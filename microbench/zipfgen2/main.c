#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <time.h>

double *m_dP;
double m_dPMax;
unsigned long m_N;
static int init_zipf_gen(const unsigned long N, const double skewness)
{
	int i;
	double dSum = 0;
	// srand((unsigned)time(NULL));
	// Build table
	m_dP = (double *)malloc(sizeof(double) * N);
	if (!m_dP) return 0;
	
	for (i = 0; i < N; i++) {
		dSum += 1.0 / pow(i + 1, skewness);
		m_dP[i] = dSum;	
	}
	m_dPMax = dSum;
	m_N = N;
	return 1;
}
static unsigned long generate_zipf_lbn(void)
{
	int i_min = 0;
	int i_max = m_N - 1;
	double sample = ((double)rand() * m_dPMax)/((double)RAND_MAX);
	// Binary search
	while (i_min + 1 < i_max) {
		int i_middle = (i_min + i_max) / 2;
		double middle = m_dP[i_middle];
		if (sample < middle) {
			i_max = i_middle;
		} else {
			i_min = i_middle;
		}
	}
	
	return i_min;
}

int main(int argc, char **argv) {
		FILE *fp;
		char file_name[1024];
		char temp_string[1024];
		int i, c;
		double alpha = 1.0;
		unsigned long range = (1UL << 10); //range
		unsigned long nr_values = (1UL << 10);
		fprintf(stderr, "\n===============================================\n");
		fprintf(stderr, "USAGE: ./zipfgen -a alpha -n nr_values -r range\n");
		fprintf(stderr, "-a: --alpha value for zipfgen\n");
		fprintf(stderr, "-r: --range for the range of zipfgen\n");
		fprintf(stderr, "-n: --nr_values for the number of generated values\n\n");

		while((c = getopt(argc, argv, "a:n:r:")) != -1) {
				switch(c) {
						case 'a':
								alpha = atof(optarg);
								break;
						case 'r':
								range = atoi(optarg);
								break;
						case 'n':
								nr_values = atoi(optarg);
								break;
				}
		}

		fprintf("GENERATE ZIPFGEN, alpha: %.1f n: %lu, nr_values: %lu\n", alpha, range, nr_values);

		sfprintf(file_name, "./output/zipfgen%.1f_n%lu_r%lu", alpha, nr_values, range);
		fprintf("output file name: %s\n", file_name);
		fp = fopen(file_name, "w");

		ffprintf(fp, "%lu\n", range);
		ffprintf(fp, "%lu\n", nr_values);

		init_zipf_gen(range, alpha);
		for(i = 0; i < nr_values; i++) 
				ffprintf(fp, "%lu\n", generate_zipf_lbn());
		fprintf("\n");
		fprintf("...........DONE!\n");

		fclose(fp);

		return 0;
}
