# CCow: Coverage Copy-on-Write

- include/linux/mm_types.h : vma struct has the data of nr_cow
- kernel/fork.c : when the process is forked, CCoW initialize and organize the struct for tracing nr_cow
- mm/memory.c : do_wp_page - check nr_cow and decide to do cow or ccow.
                do_wp_sfork, sfork_one_pte - do CCoW!
