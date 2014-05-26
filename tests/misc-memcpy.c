/*
* Copyright (C) 2014 Cloudius Systems, Ltd.
*
* This work is open source software, licensed under the terms of the
* BSD license as described in the LICENSE file in the top-level directory.
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <math.h>
#include <unistd.h>
#include <limits.h>


#define MIN_SIZE 4
#define MAX_SIZE (32 << 10)
#define LOOPS 1000000
#define RUNS 30

static float vector[RUNS];

static unsigned long gtime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000000 + tv.tv_usec * 1000;
}

void statistics(const char *name, int size)
{
    float min, max, mean, stdev;
    min = INT_MAX;
    max = 0;
    mean = 0;
    stdev = 0;
    int r;

    for (r = 0; r < RUNS; ++r) {
        mean += vector[r];
        if (vector[r] < min) {
            min = vector[r];
        }

        if (vector[r] > max) {
            max = vector[r];
        }
    }
    mean = mean / RUNS;

    for (r = 0; r < RUNS; ++r) {
        float d = vector[r];
        stdev += (d - mean) * (d - mean);
    }
    stdev = sqrtf(stdev / RUNS);

    printf("%s,%d,%f,%f,%f,%f\n", name, size, min, max, mean, stdev);

}

void test_memcpy(size_t size)
{
    void *src = malloc(size);
    void *dest= malloc(size);
    int r, i;

    memset(src, 'c', size);

    for (r = 0; r < RUNS; ++r) {
        unsigned long t1 = gtime();
        for (i= 0; i < LOOPS; ++i) {
            memcpy(dest, src, size);
        }
        unsigned long t2 = gtime();

        vector[r] = (float)(t2-t1) / LOOPS;
    }

    statistics("memcpy", size);

    free(src);
    free(dest);
}

void test_memset(size_t size)
{
    void *buf= malloc(size);
    int r, i;


    for (r = 0; r < RUNS; ++r) {
        unsigned long t1 = gtime();
        for (i= 0; i < LOOPS; ++i) {
            memset(buf, 'c', size);
        }
        unsigned long t2 = gtime();

        vector[r] = (float)(t2-t1) / LOOPS;
    }

    statistics("memset", size);

    free(buf);
}

int main()
{
    size_t i;
    size_t sizes[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 15, 16, 17,
            31, 32, 33, 64, 128, 255, 256, 257, 512, 1024, 2048,
            4096, 8192, 16386, 32768
    };
    size_t nsizes = sizeof(sizes) / sizeof(*sizes);
    for (i = 0; i < nsizes; ++i) {
        test_memcpy(sizes[i]);
    }

    for (i = 0; i < nsizes; ++i) {
        test_memset(sizes[i]);
    }


    return 0;
}
