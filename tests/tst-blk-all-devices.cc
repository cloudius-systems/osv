/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Write, flush and read back a pattern on each block device named on the
// command line, from a thread pinned to every CPU in turn, so that every IO
// queue a multiqueue driver created is exercised.  A queue whose completion
// interrupt was never set up hangs the test instead of passing it.
//
// Usage: tst-blk-all-devices.so /dev/vblk1 [/dev/vblk2 ...].  Each device's
// first 4 KiB is overwritten: give it scratch disks.

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int check(const char *dev, int fd, int cpu)
{
    const size_t len = 4096;
    char *w = static_cast<char*>(aligned_alloc(len, len));
    char *r = static_cast<char*>(aligned_alloc(len, len));
    for (size_t i = 0; i < len; i++) {
        w[i] = static_cast<char>(i * 7 + cpu);
    }
    memset(r, 0, len);
    int ok = pwrite(fd, w, len, 0) == (ssize_t)len && fsync(fd) == 0 &&
             pread(fd, r, len, 0) == (ssize_t)len && memcmp(w, r, len) == 0;
    if (!ok) {
        printf("FAIL: %s on cpu %d\n", dev, cpu);
    }
    free(w);
    free(r);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("SKIP: no devices given\n");
        return 0;
    }
    int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    int failures = 0;
    for (int d = 1; d < argc; d++) {
        int fd = open(argv[d], O_RDWR);
        if (fd < 0) {
            printf("FAIL: cannot open %s\n", argv[d]);
            failures++;
            continue;
        }
        for (int cpu = 0; cpu < ncpus; cpu++) {
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(cpu, &set);
            if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) {
                printf("FAIL: cannot pin to cpu %d\n", cpu);
                failures++;
                break;
            }
            failures += !check(argv[d], fd, cpu);
        }
        close(fd);
    }
    printf("%d devices, %d cpus, %d failures\n", argc - 1, ncpus, failures);
    if (failures == 0) {
        printf("PASS\n");
    }
    return failures ? 1 : 0;
}
