// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include <linux/if_vlan.h>
#include <linux/dsa/sja1105.h>
#include <linux/dsa/8021q.h>
#include <linux/packing.h>
#include "dsa_priv.h"

/* Similar to is_link_local_ether_addr(hdr->h_dest) but also covers PTP */
static inline bool sja1105_is_link_local(const struct sk_buff *skb)
{
	const struct ethhdr *hdr = eth_hdr(skb);
	u64 dmac = ether_addr_to_u64(hdr->h_dest);

	if (ntohs(hdr->h_proto) == ETH_P_SJA1105_META)
		return false;
	if ((dmac & SJA1105_LINKLOCAL_FILTER_A_MASK) ==
		    SJA1105_LINKLOCAL_FILTER_A)
		return true;
	if ((dmac & SJA1105_LINKLOCAL_FILTER_B_MASK) ==
		    SJA1105_LINKLOCAL_FILTER_B)
		return true;
	return false;
}

struct sja1105_meta {
	u64 tstamp;
	u64 dmac_byte_4;
	u64 dmac_byte_3;
	u64 source_port;
	u64 switch_id;
};

static void sja1105_meta_unpack(const struct sk_buff *skb,
				struct sja1105_meta *meta)
{
	u8 *buf = skb_mac_header(skb) + ETH_HLEN;

	/* UM10944.pdf section 4.2.17 AVB Parameters:
	 * Structure of the meta-data follow-up frame.
	 * It is in network byte order, so there are no quirks
	 * while unpacking the meta frame.
	 *
	 * Also SJA1105 E/T only populates bits 23:0 of the timestamp
	 * whereas P/Q/R/S does 32 bits. Since the structure is the
	 * same and the E/T puts zeroes in the high-order byte, use
	 * a unified unpacking command for both device series.
	 */
	packing(buf,     &meta->tstamp,     31, 0, 4, UNPACK, 0);
	packing(buf + 4, &meta->dmac_byte_4, 7, 0, 1, UNPACK, 0);
	packing(buf + 5, &meta->dmac_byte_3, 7, 0, 1, UNPACK, 0);
	packing(buf + 6, &meta->source_port, 7, 0, 1, UNPACK, 0);
	packing(buf + 7, &meta->switch_id,   7, 0, 1, UNPACK, 0);
}

static inline bool sja1105_is_meta_frame(const struct sk_buff *skb)
{
	const struct ethhdr *hdr = eth_hdr(skb);
	u64 smac = ether_addr_to_u64(hdr->h_source);
	u64 dmac = ether_addr_to_u64(hdr->h_dest);

	if (smac != SJA1105_META_SMAC)
		return false;
	if (dmac != SJA1105_META_DMAC)
		return false;
	if (ntohs(hdr->h_proto) != ETH_P_SJA1105_META)
		return false;
	return true;
}

/* This is the first time the tagger sees the frame on RX.
 * Figure out if we can decode it.
 */
static bool sja1105_filter(const struct sk_buff *skb, struct net_device *dev)
{
	if (!dsa_port_is_vlan_filtering(dev->dsa_ptr))
		return true;
	if (sja1105_is_link_local(skb))
		return true;
	if (sja1105_is_meta_frame(skb))
		return true;
	return false;
}

/* Calls sja1105_port_deferred_xmit in sja1105_main.c */
static struct sk_buff *sja1105_defer_xmit(struct sja1105_port *sp,
					  struct sk_buff *skb)
{
	/* Increase refcount so the kfree_skb in dsa_slave_xmit
	 * won't really free the packet.
	 */
	skb_queue_tail(&sp->xmit_queue, skb_get(skb));
	kthread_queue_work(sp->xmit_worker, &sp->xmit_work);

	return NULL;
}

static struct sk_buff *sja1105_xmit(struct sk_buff *skb,
				    struct net_device *netdev)
{
	struct dsa_port *dp = dsa_slave_to_port(netdev);
	u16 tx_vid = dsa_8021q_tx_vid(dp->ds, dp->index);
	u16 queue_mapping = skb_get_queue_mapping(skb);
	u8 pcp = netdev_txq_to_tc(netdev, queue_mapping);

	/* Transmitting management traffic does not rely upon switch tagging,
	 * but instead SPI-installed management routes. Part 2 of this
	 * is the .port_deferred_xmit driver callback.
	 */
	if (unlikely(sja1105_is_link_local(skb)))
		return sja1105_defer_xmit(dp->priv, skb);

	/* If we are under a vlan_filtering bridge, IP termination on
	 * switch ports based on 802.1Q tags is simply too brittle to
	 * be passable. So just defer to the dsa_slave_notag_xmit
	 * implementation.
	 */
	if (dsa_port_is_vlan_filtering(dp))
		return skb;

	return dsa_8021q_xmit(skb, netdev, ETH_P_SJA1105,
			     ((pcp << VLAN_PRIO_SHIFT) | tx_vid));
}

static void sja1105_transfer_meta(struct sk_buff *skb,
				  const struct sja1105_meta *meta)
{
	struct ethhdr *hdr = eth_hdr(skb);

	hdr->h_dest[3] = meta->dmac_byte_3;
	hdr->h_dest[4] = meta->dmac_byte_4;
	SJA1105_SKB_CB(skb)->meta_tstamp = meta->tstamp;
}

/* This is a simple state machine which follows the hardware mechanism of
 * generating RX timestamps:
 *
 * After each timestampable skb (all traffic for which send_meta1 and
 * send_meta0 is true, aka all MAC-filtered link-local traffic) a meta frame
 * containing a partial timestamp is immediately generated by the switch and
 * sent as a follow-up to the link-local frame on the CPU port.
 *
 * The meta frames have no unique identifier (such as sequence number) by which
 * one may pair them to the correct timestampable frame.
 * Instead, the switch has internal logic that ensures no frames are sent on
 * the CPU port between a link-local timestampable frame and its corresponding
 * meta follow-up. It also ensures strict ordering between ports (lower ports
 * have higher priority towards the CPU port). For this reason, a per-port
 * data structure is not needed/desirable.
 *
 * This function pairs the link-local frame with its partial timestamp from the
 * meta follow-up frame. The full timestamp will be reconstructed later in a
 * work queue.
 */
static struct sk_buff
*sja1105_rcv_meta_state_machine(struct sk_buff *skb,
				struct sja1105_meta *meta,
				bool is_link_local,
				bool is_meta)
{
	struct sja1105_port *sp;
	struct dsa_port *dp;

	dp = dsa_slave_to_port(skb->dev);
	sp = dp->priv;

	/* Step 1: A timestampable frame was received.
	 * Buffer it until we get its meta frame.
	 */
	if (is_link_local) {
		if (!test_bit(SJA1105_HWTS_RX_EN, &sp->data->state))
			/* Do normal processing. */
			return skb;

		spin_lock(&sp->data->meta_lock);
		/* Was this a link-local frame instead of the meta
		 * that we were expecting?
		 */
		if (sp->data->stampable_skb) {
			dev_err_ratelimited(dp->ds->dev,
					    "Expected meta frame, is %12llx "
					    "in the DSA master multicast filter?\n",
					    SJA1105_META_DMAC);
			kfree_skb(sp->data->stampable_skb);
		}

		/* Hold a reference to avoid dsa_switch_rcv
		 * from freeing the skb.
		 */
		sp->data->stampable_skb = skb_get(skb);
		spin_unlock(&sp->data->meta_lock);

		/* Tell DSA we got nothing */
		return NULL;

	/* Step 2: The meta frame arrived.
	 * Time to take the stampable skb out of the closet, annotate it
	 * with the partial timestamp, and pretend that we received it
	 * just now (basically masquerade the buffered frame as the meta
	 * frame, which serves no further purpose).
	 */
	} else if (is_meta) {
		struct sk_buff *stampable_skb;

		/* Drop the meta frame if we're not in the right state
		 * to process it.
		 */
		if (!test_bit(SJA1105_HWTS_RX_EN, &sp->data->state))
			return NULL;

		spin_lock(&sp->data->meta_lock);

		stampable_skb = sp->data->stampable_skb;
		sp->data->stampable_skb = NULL;

		/* Was this a meta frame instead of the link-local
		 * that we were expecting?
		 */
		if (!stampable_skb) {
			dev_err_ratelimited(dp->ds->dev,
					    "Unexpected meta frame\n");
			spin_unlock(&sp->data->meta_lock);
			return NULL;
		}

		if (stampable_skb->dev != skb->dev) {
			dev_err_ratelimited(dp->ds->dev,
					    "Meta frame on wrong port\n");
			spin_unlock(&sp->data->meta_lock);
			return NULL;
		}

		/* Free the meta frame and give DSA the buffered stampable_skb
		 * for further processing up the network stack.
		 */
		kfree_skb(skb);
		skb = stampable_skb;
		sja1105_transfer_meta(skb, meta);

		spin_unlock(&sp->data->meta_lock);
	}

	return skb;
}

static struct sk_buff *sja1105_rcv(struct sk_buff *skb,
				   struct net_device *netdev,
				   struct packet_type *pt)
{
	struct sja1105_meta meta = {0};
	int source_port, switch_id;
	struct ethhdr *hdr;
	u16 tpid, vid, tci;
	bool is_link_local;
	bool is_tagged;
	bool is_meta;

	hdr = eth_hdr(skb);
	tpid = ntohs(hdr->h_proto);
	is_tagged = (tpid == ETH_P_SJA1105);
	is_link_local = sja1105_is_link_local(skb);
	is_meta = sja1105_is_meta_frame(skb);

	skb->offload_fwd_mark = 1;

	if (is_tagged) {
		/* Normal traffic path. */
		skb_push_rcsum(skb, ETH_HLEN);
		__skb_vlan_pop(skb, &tci);
		skb_pull_rcsum(skb, ETH_HLEN);
		skb_reset_network_header(skb);
		skb_reset_transport_header(skb);

		vid = tci & VLAN_VID_MASK;
		source_port = dsa_8021q_rx_source_port(vid);
		switch_id = dsa_8021q_rx_switch_id(vid);
		skb->priority = (tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
	} else if (is_link_local) {
		/* Management traffic path. Switch embeds the switch ID and
		 * port ID into bytes of the destination MAC, courtesy of
		 * the incl_srcpt options.
		 */
		source_port = hdr->h_dest[3];
		switch_id = hdr->h_dest[4];
		/* Clear the DMAC bytes that were mangled by the switch */
		hdr->h_dest[3] = 0;
		hdr->h_dest[4] = 0;
	} else if (is_meta) {
		sja1105_meta_unpack(skb, &meta);
		source_port = meta.source_port;
		switch_id = meta.switch_id;
	} else {
		return NULL;
	}

	skb->dev = dsa_master_find_slave(netdev, switch_id, source_port);
	if (!skb->dev) {
		netdev_warn(netdev, "Couldn't decode source port\n");
		return NULL;
	}

	return sja1105_rcv_meta_state_machine(skb, &meta, is_link_local,
					      is_meta);
}

static struct dsa_device_ops sja1105_netdev_ops = {
	.name = "sja1105",
	.proto = DSA_TAG_PROTO_SJA1105,
	.xmit = sja1105_xmit,
	.rcv = sja1105_rcv,
	.filter = sja1105_filter,
	.overhead = VLAN_HLEN,
};

MODULE_LICENSE("GPL v2");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_SJA1105);

module_dsa_tag_driver(sja1105_netdev_ops);
