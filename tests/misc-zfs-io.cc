/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "stat.hh"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <chrono>

#define MB (1024 * 1024)
#define BUF_SIZE 4096

extern "C" uint64_t kmem_size(void);
static std::chrono::high_resolution_clock s_clock;

static void seq_write(int fd, char *buf, unsigned long size, unsigned long offset)
{
    auto start_time = s_clock.now();
    unsigned long to_write, bytes;

    printf("ZFS: Writing %dMB to the file starting at the offset %dMB...\n",
           size / MB, offset / MB);

    assert(lseek(fd, offset, SEEK_SET) >= 0);
    bytes = 0;
    to_write = size;

    while (to_write > 0) {
        int written_bytes = write(fd, buf, MIN(to_write, BUF_SIZE));
        to_write -= written_bytes;
        bytes += written_bytes;
    }

    auto end_time = s_clock.now();
    auto duration = to_seconds(end_time - start_time);
    printf("\t* Wrote %.3f MB in %.2f seconds = %.3f MB/s\n",
        (double) size / MB, duration, (double) size / MB / duration);
}

static void seq_read(int fd, char *buf, unsigned long size, unsigned long offset)
{
    auto start_time = s_clock.now();
    unsigned long to_read, bytes;

    printf("ZFS: Reading %dMB from the file starting at the offset %dMB...\n",
           size / MB, offset / MB);

    assert(lseek(fd, offset, SEEK_SET) >= 0);
    bytes = 0;
    to_read = size;

    while (to_read > 0) {
        int read_bytes = read(fd, buf, MIN(to_read, BUF_SIZE));
        to_read -= read_bytes;
        bytes += read_bytes;
    }

    auto end_time = s_clock.now();
    auto duration = to_seconds(end_time - start_time);
    printf("\t* Read %.3f MB in %.2f seconds = %.3f MB/s\n",
        (double) size / MB, duration, (double) size / MB / duration);
}

int main(int argc, char **argv)
{
    char fpath[64] = "/zfs-io-file";
    char buf[BUF_SIZE];
    unsigned size;
    int fd;
    bool random = false;
    bool rdonly = false;
    bool all_cached = false;
    bool unlink_file = true;

    for (int i = 1; i < argc; i++) {
        if (!strcmp("--random", argv[i])) {
            random = true;
        } else if (!strcmp("--rdonly", argv[i])) {
            rdonly = true;
        } else if (!strcmp("--all-cached", argv[i])) {
            all_cached = true;
        } else if (!strcmp("--no-unlink", argv[i])) {
            unlink_file = false;
        }
    }

    if (all_cached) {
        size = kmem_size() * 40U / 100U;
    } else if (random) {
        size = kmem_size();
    } else {
        size = kmem_size() + (kmem_size() * 50U / 100U);
    }

    memset(buf, 0xAB, BUF_SIZE);
    fd = open(fpath, O_CREAT | O_RDWR | O_LARGEFILE);
    assert(fd > 0);

    ftruncate(fd, size);

    if (rdonly) {
        seq_read(fd, buf, size, 0UL);
    } else {
        seq_write(fd, buf, size, 0UL);
    }

    if (random) {
       /*
        * Let's virtually split the file into 64-mb chunks to reproduce a
        * non-linear workload.
        */
        unsigned nr_64_mb_chunks = (size / (64*MB)) - 1;
        printf("Size of file:          %lu\n", size);
        printf("Number of 64MB chunks: %lu\n", nr_64_mb_chunks);

        std::srand(std::time(0));
        memset(buf, 0xBA, BUF_SIZE);

       /*
        * Randomly touch an arbitrary amount of 64-mb chunks throughout the file.
        * Previously, the entire file was touched, likewise most of the file
        * remains in ARC. Cache hits would take the respective cache entries
        * into the MFU list.
        */
        for (unsigned i = 0U; i < nr_64_mb_chunks / 2U; i++) {
            /* offsets generated in 64-mb granularity */
            unsigned long offset = (rand() % nr_64_mb_chunks) * (64*MB);
            seq_write(fd, buf, 64*MB, offset);
        }
    }

    close(fd);
    if (unlink_file) {
        unlink("/zfs-io-file");
    }

    return 0;
}
