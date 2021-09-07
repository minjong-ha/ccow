# CCow: Coverage Copy-on-Write

- include/linux/mm_types.h : vma struct has the data for ccow
- kernel/fork.c : when the process is forked, CCoW initialize and organize the struct for tracing ccow
- mm/memory.c : do_wp_page - check nr_cow or nr_dirty_bits and decide to do cow or ccow.
                do_wp_sfork, sfork_one_pte - do CCoW!

### COMMIT HISTORY
		Since the original repository is a private, labaratory repository, I logged the commit histories in this section instead.
		Check "commit.log"file in the repository
