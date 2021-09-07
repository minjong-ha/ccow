#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>

#include <linux/kernel_stat.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/numa_balancing.h>
#include <linux/sched/task.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/memremap.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/export.h>
#include <linux/delayacct.h>
#include <linux/init.h>
#include <linux/pfn_t.h>
#include <linux/writeback.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/swapops.h>
#include <linux/elf.h>
#include <linux/gfp.h>
#include <linux/migrate.h>
#include <linux/string.h>
#include <linux/dma-debug.h>
#include <linux/debugfs.h>
#include <linux/userfaultfd_k.h>
#include <linux/dax.h>
#include <linux/oom.h>
#include <linux/numa.h>



#include <linux/slab.h>
#include <linux/hash.h>

#include "internal.h"

static struct inode *process_inode;

static void *m_start (struct seq_file *m, loff_t *ppos) {
		seq_printf(m, "m_start!\n");
		return 0;
}

static void *m_next (struct seq_file *m, void *v, loff_t *ppos) {
		seq_printf(m, "m_next!\n");
		return 0;
}

static void m_stop (struct seq_file *m, void *v) {
		seq_printf(m, "m_stop!\n");
		return 0;
}

static int show_sfork(struct seq_file *m, void *v) {
		seq_printf(m, "show_sfork!\n");
		return 0;
}
static const struct seq_operations proc_pid_sfork_op = {
		.start = m_start,
		.next = m_next,
		.stop = m_stop,
		.show = show_sfork
};

static int proc_sfork_open(struct seq_file *m, void *v) {
		int i = 0;
		int j = 0;
		struct inode *inode = m->private;
		//struct pid_namespace *ns = proc_pid_ns(inode);
		struct pid *pid = proc_pid(inode);
		struct task_struct *task;

		struct vm_area_struct *vma = NULL;
		unsigned long vpage = NULL;

		unsigned bkt;
		struct h_node_sfork *cur;

		unsigned long total_fault = 0;
		unsigned long total_cow = 0;
		unsigned long total_sfork = 0;

		unsigned long total_sum_a = 0;
		unsigned long total_sum_b = 0;
		unsigned long total_sum_c = 0;
		unsigned long total_nr_a = 0;
		unsigned long total_nr_b = 0;
		unsigned long total_nr_c = 0;
		

		task = get_pid_task(pid, PIDTYPE_PID);
		if (!task)
				return -ESRCH;

		put_task_struct(task);

		//seq_printf(m, "pid: %d task_struct: %p task_state: %ld task->sfork: %d mm: %p %p\n", task->pid, task, task->state, task->sfork, task->mm, task->active_mm);

		if(task->mm && task->mm->mmap) {
				int tmp = 0;
				unsigned long nr_sfm = 0;
				unsigned long nr_tail_entry = 0;
				struct sfm_node *curr = NULL;
				struct vm_area_struct *max_vma = NULL;
				struct sfm_list *max_list = NULL;
				int max = 0;

				for(vma = task->mm->mmap; vma; vma = vma->vm_next) {
						total_cow = total_cow + vma->nr_total_cow;
						total_sfork = total_sfork + vma->nr_total_sfork;

						seq_printf(m, "vma: %lx nr_total_cow: %lu nr_total_sfork: %lu vma_size: %lu\n", 
										vma, vma->nr_total_cow, vma->nr_total_sfork, (vma->vm_end - vma->vm_start));

//						if(vma->sfork_map_list != NULL) {
//								seq_printf(m, "sfork_map_list: %lx head: %lx tail: %lx nr_total_cow: %lu nr_cow: %lu nr_total_sfork: %lu nr_sfork: %lu\n", vma->sfork_map_list, vma->sfork_map_list->head, vma->sfork_map_list->tail, vma->nr_total_cow, vma->nr_cow, vma->nr_total_sfork, vma->nr_sfork);
//
//								total_fault += vma->nr_total_cow;
//								total_fault += vma->nr_total_sfork;
//								if(max < (vma->nr_total_sfork + vma->nr_total_cow)) {
//										max = vma->nr_total_sfork + vma->nr_total_cow;
//										max_vma = vma;
//								}
//
//								nr_tail_entry = vma->nr_last_entry;
//								curr = vma->sfork_map_list->head;
//								seq_printf(m, "%lx -> ", curr);
//
//								while(curr->next_sfm != NULL) {
//										curr = curr->next_sfm;
//										seq_printf(m, "%lx -> ", curr);
//								}
//
//								curr = vma->sfork_map_list->head;
//
//								seq_printf(m, "NULL\n");
//
//								if(vma->nr_total_sfork > 5000) {
//										while(curr->next_sfm != NULL) {
//												for(i = 0; i < SFORK_MAP_LIMIT; i++) {
//														seq_printf(m, "%lu,", curr->sfork_map[i]);
//												}
//												nr_tail_entry = nr_tail_entry - SFORK_MAP_LIMIT;
//												curr = curr->next_sfm;
//										}
//										if(nr_tail_entry > 0) {
//												for(i = 0; i < nr_tail_entry; i++) {
//														seq_printf(m, "%lu,", curr->sfork_map[i]);
//												}
//												seq_printf(m, "\n");
//										}
//								}
//						}
				}
				seq_printf(m, "==================================================================\n");
//				nr_tail_entry = 0;
//				//from here!
//				for(vma = task->mm->mmap; vma; vma = vma->vm_next) {
//						if(vma->nr_total_sfork > 1000) {
//								seq_printf(m, "vma: %lx nr_last_sfm: %lu nr_last_entry: %lu nr last_vma_size: %lu nr_total_cow: %lu nr_total_ccow: %lu\n", 
//												vma, vma->nr_last_sfm, vma->nr_last_entry, vma->last_vma_size, vma->nr_total_cow, vma->nr_total_sfork);
//								list_for_each_entry(max_list, &vma->sfm_history, list) {
//										if(max_list->nr_sfork > 0) {
//												seq_printf(m, "nr_cow: %lu nr_sfork: %lu nr_total: %lu sfork_rate: %lu changing valid_rate to : %lu\n", 
//																max_list->nr_cow, max_list->nr_sfork, max_list->nr_cow + max_list->nr_sfork,
//																max_list->nr_sfork * 100 / (max_list->nr_cow + max_list->nr_sfork), max_list->valid_rate);
//										}
//								}
//								seq_printf(m, "===========================================================================\n");
//						}
//				}

				seq_printf(m, "process total cow: %lu total sfork: %lu\n", total_cow, total_sfork);
//				if(total_sum_a > 0 && total_nr_a > 0)
//						seq_printf(m, "avg sum_a: %lu\n", total_sum_a / total_nr_a);
//				if(total_sum_b > 0 && total_nr_b > 0)
//						seq_printf(m, "avg sum_b: %lu\n", total_sum_b / total_nr_b);
//				if(total_sum_c > 0 && total_nr_c > 0)
//						seq_printf(m, "avg sum_c: %lu\n", total_sum_c / total_nr_c);
		}
		seq_printf(m, "total_fault: %lu\n", total_fault);

		return 0;
}

static int pid_sfork_open(struct inode *inode, struct file *file) {
		//		printk(KERN_DEBUG "pid_sfork_open!\n");
		//		printk(KERN_DEBUG "inode: %p, file: %p\n", inode, file);
		process_inode = inode;

		return single_open(file, proc_sfork_open, inode);
}

static int proc_sfork_release(struct inode *inode, struct file *file) {
		printk(KERN_DEBUG "proc_sfork_release!\n");

		return 0;
}

//buf: string from user, count: length of string from user
static ssize_t proc_sfork_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
		int ret;
		void *kernel_buf;

		struct task_struct *task;
		struct pid *pid = proc_pid(process_inode);

		ret = 0;

		kernel_buf = kmalloc(count-1, GFP_KERNEL);
		memset(kernel_buf, 0, count-1);
		ret = copy_from_user(kernel_buf, buf, count-1);

		task = get_pid_task(pid, PIDTYPE_PID);
		if (!task)
				return -ESRCH;

		printk(KERN_INFO "count: %ld\n", count-1);

		if((count-1) == 0 || (count-1) < 0)
				return count;

		ret = strncmp(kernel_buf, "INC", count-1);

		if(ret == 0) {
				task->sfork++;
				printk(KERN_INFO "INC sfork\n");
		}
		else{
				task->sfork--;
				printk(KERN_INFO "DEC sfork\n");
		}

		printk(KERN_INFO "task->sfork: %d\n", task->sfork);

		return count;
}

//seq_read, seq_lseek are included in seq_file.c
const struct file_operations proc_pid_sfork_operations = {
		.open = pid_sfork_open,
		.write = proc_sfork_write,
		.read = seq_read,
		.llseek = seq_lseek,
		.release = proc_sfork_release,
};

