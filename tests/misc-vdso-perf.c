/*
 * Copyright (C) 2023 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/* Simple test program intended to measure performance of VDSO
   based functions like clock_gettime(), gettimeofday() and time().
   It will be useful to evaluate overhead of future changes to support
   running statically linked executables on OSv.

 - build as static executable:
   gcc -o misc-vdso-perf-static tests/misc-vdso-perf.c -static

 - build as static PIE:
   gcc -o misc-vdso-perf-static-pie tests/misc-vdso-perf.c -pie -static-pie
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <syscall.h>
#include <assert.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

uint64_t nstime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t mul = 1000000000, mul2 = 1000;
    return tv.tv_sec * mul + tv.tv_usec * mul2;
}

int main(int argc, char **argv)
{
    long count = 50000000;
    long loop = count;
    uint64_t start = nstime();

    struct timespec ts1;
    while (loop--) {
        assert(0 == clock_gettime(CLOCK_MONOTONIC, &ts1));
    }

    uint64_t end = nstime();

    long average_syscall_duration = (end - start) / count;
    printf("%lu ns (elapsed %.2f sec) %s\n", average_syscall_duration, (end - start) / 1000000000.0, ": average clock_gettime duration");
}

