#include <assert.h>             // Needed for assert() macro
#include <stdio.h>              // Needed for printf()
#include <stdlib.h>             // Needed for exit() and ato*()
#include <math.h>               // Needed for pow()
#include <unistd.h>
#include <string.h>
#include <time.h>

#define  FALSE          0       // Boolean false
#define  TRUE           1       // Boolean true

#define CYCLE 20UL
#define MAX_RANDOM (1UL << 8)

#define SIZE_VAL 1024


size_t zmalloc_get_smap_bytes_by_field(char *field, long pid) {
		char line[1024];
		size_t bytes = 0;
		int flen = strlen(field);
		FILE *fp;

		if (pid == -1) 
				fp = fopen("/proc/self/smaps", "r");
		else {
				char filename[128];
				snprintf(filename, sizeof(filename), "/proc/%ld/smaps", pid);
				fp = fopen(filename, "r");
		}

		if(!fp) return 0;
		while(fgets(line, sizeof(line), fp) != NULL) {
				if(strncmp(line, field, flen) == 0) {
						char *p = strchr(line, 'k');
						if(p) {
								*p = '\0';
								bytes += strtol(line + flen, NULL, 10) * 1024;
						}
				}
		}
		fclose(fp);

		return bytes;
}

size_t zmalloc_get_private_dirty(long pid) {
		return zmalloc_get_smap_bytes_by_field("Private_Dirty:", pid);
}

int main(int argc, char *argv[]) {
		FILE *fp;
		char *file_name;
		char tmp[1024];
		unsigned long    i, j, x;                     // Loop counter
		int c;
		size_t cow_size;
		size_t nr_values;
		size_t range;
		size_t nr_wr_count = 0;
		clock_t start, end;
		clock_t wr_start, wr_end;
		clock_t fork_start, fork_end;
		float check, wr_check, fork_check;
		pid_t pid, wpid;
		int fork_period = 5; //default 5 sec
		int status;
		unsigned long *seq_entry;

		file_name = (char *)malloc(1024);

		while((c = getopt(argc, argv, "p:f:")) != -1) {
				switch(c) {
						case 'p': printf("fork period: %f sec\n", atof(optarg));
								fork_period = atof(optarg);
								break;
						case 'f':
								file_name = strdup(optarg);
								break;
				}
		}

		printf("file_name: %s\n", file_name);
		fflush(stdout);
		if(!file_name) {
				printf("ERROR: NO INPUT FILE FOR ZIPF\n");
				exit(0);
		}
		printf("fork_period: %lu file_name: %s\n", fork_period, file_name);
		fflush(stdout);

		fp = fopen(file_name, "r");
		range = (size_t)atoi(fgets(tmp, sizeof(tmp), fp));
		nr_values = (size_t)atoi(fgets(tmp, sizeof(tmp), fp));
		printf("nr_values: %lu range: %lu\n", nr_values, range);
		fflush(stdout);
		seq_entry = (unsigned long *)malloc(sizeof(unsigned long) * nr_values);
		i = 0;
		while(fgets(tmp, sizeof(tmp), fp)) {
				x = atoi(tmp);
				seq_entry[i] = x;
				i++;
		}
		fclose(fp);


		printf("======GENERATING TABLE======\n");
		fflush(stdout);
		start = clock();
		unsigned long *table;
		table = (unsigned long *)malloc(range * 8UL);
		printf("size of table: %lu\n", range * 8UL);
		fflush(stdout);
		for(i = 0; i < range; i++) {
				char *tmp = (char *)malloc(SIZE_VAL);
				memset(tmp, 9, SIZE_VAL);
				table[i] = (unsigned long)tmp;
		}

//		for(i = 0; i < range; i++) {
//				char data_tmp[SIZE_VAL];
//				sprintf(data_tmp, "I UPDATE THIS %lu SHIT!!!!!!%lu", seq_entry[i], rand());
//				char *table_entry = (char *)table[i];
//				printf("table[%d]: %lx, table_entry: %p\n", i, table[i], table_entry);
//				memcpy(table_entry, data_tmp, SIZE_VAL);
//				printf("table[%d]: %lx, table_entry: %p\n", i, table[i], table_entry);
//		}
//
//		for(i = 0; i < range; i++) {
//				char *table_entry = (char *)table[i];
//				printf("table[%d]: %lx, %s\n", i, table[i], table_entry);
//		}

		printf("total size: %lu\n", nr_values * SIZE_VAL);
		fflush(stdout);
		end = clock();
		check = (float)(end - start) / CLOCKS_PER_SEC;
		printf("time: %lf sec\n", check);
		fflush(stdout);

		//seed in rand_val() determines the output zipf values
		printf("======CONFIGURATE SEED======\n");
		fflush(stdout);
		start = clock();
		end = clock();
		check = (float)(end - start) / CLOCKS_PER_SEC;
		printf("time: %lf sec\n", check);
		fflush(stdout);

		printf("======WRTING TABLE ENTRIES======\n");
		start = clock();
		wr_start = clock();
		fork_start = clock();
		for(j = 0; j < CYCLE; j++) {
				for(i=0; i < nr_values; i++) {
						int x;
						//char data_tmp[SIZE_VAL];
						unsigned long data_tmp = rand();
						char *tmp;
						//sprintf(data_tmp, "I UPDATE THIS %lu SHIT!!!!!!%lu", seq_entry[i], rand());
//						for(x = 0; x < SIZE_VAL; x++) {
//								data_tmp[x] = rand();
//						}
						tmp = (char *)table[seq_entry[i]];
						//memcpy(tmp, data_tmp, SIZE_VAL);
						memcpy(tmp, &data_tmp, sizeof(unsigned long));
						nr_wr_count++;
						wr_end = clock();
						wr_check = (float)(wr_end - wr_start) / CLOCKS_PER_SEC;
						fork_check = (float)(wr_end - fork_start) / CLOCKS_PER_SEC;
						if(wr_check > 1.0) {
								printf("wr/sec: %lu\n", nr_wr_count);
								fflush(stdout);
								nr_wr_count = 0;
								wr_start = clock();
						}
						if(fork_check > fork_period) {
								cow_size = zmalloc_get_private_dirty(-1);
								printf("FORK!!! %lu memory used for copy-on-write\n", cow_size/(1024*1024));
								fflush(stdout);
								pid = fork();

								if(pid == 0) {
										//child
										//do nothing
										sleep(fork_period * 2);
										exit(0);
								}
								else {
										//parent
										fork_start = clock();
								}
						//		while((wpid = wait(&status)) > 0);
						}
				}
		}
		end = clock();
		check = (float)(end - start) / CLOCKS_PER_SEC;
		printf("executing time: %lf sec\n", check);
		fflush(stdout);

		free(table);
		free(seq_entry);

		return 0;
}


