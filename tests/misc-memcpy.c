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

    float min, max, mean, stdev;
    min = INT_MAX;
    max = 0;
    mean = 0;
    stdev = 0;

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

    printf("%d,%f,%f,%f,%f\n", size, min, max, mean, stdev);

    free(src);
    free(dest);
}


int main()
{
    size_t i;
    for (i = MIN_SIZE; i <= MAX_SIZE; i <<= 1) {
        test_memcpy(i);
    }

    return 0;
}
