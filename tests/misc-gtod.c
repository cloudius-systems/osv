#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>


#define RUNS 100000000

unsigned long to_usec(struct timeval tv)
{
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

int main(int argc, char **argv)
{
    struct timeval tv_start;
    struct timeval tv;
    double diff;
    int i;

    gettimeofday(&tv_start, NULL);
    for (i = 0; i < RUNS; ++i) {
        gettimeofday(&tv, NULL);
    }
    gettimeofday(&tv, NULL);

    diff = (1000 * (to_usec(tv) - to_usec(tv_start))) / RUNS;
    printf("1 GTOD run: %.2f ns\n", diff);
}
