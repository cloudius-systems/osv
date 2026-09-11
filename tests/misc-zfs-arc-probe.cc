/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Measure how much of a file a ZFS root caches.  Run with one argument:
//   create  write a 192 MiB file of incompressible data;
//   read    read it three times, so that the ARC caches as much of it as its
//           ceiling allows, then print how much free memory the reads used;
//   delete  remove it.
// scripts/tests/test_zfs_arc_max.py creates the file in one boot, reads it in
// fresh boots (a cold cache) with and without --zfs-arc-max, and deletes it
// in the last boot, so the test image does not keep it.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysinfo.h>

static const char *path = "/tmp/misc-zfs-arc-probe.dat";
static const size_t file_size = 192 << 20;
static char buf[1 << 20];

static unsigned long long free_bytes()
{
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        perror("sysinfo");
        return 0;
    }
    return (unsigned long long)info.freeram * info.mem_unit;
}

// Write the file with xorshift output, which no compressor shrinks.
static bool create_file()
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open for write");
        return false;
    }
    uint64_t x = 88172645463325252ULL;
    for (size_t off = 0; off < file_size; off += sizeof(buf)) {
        for (size_t i = 0; i < sizeof(buf) / sizeof(x); i++) {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            reinterpret_cast<uint64_t *>(buf)[i] = x;
        }
        if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
            perror("write");
            close(fd);
            return false;
        }
    }
    bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

// Read the file three times and print the free memory the reads used up.
static int read_file()
{
    unsigned long long before = free_bytes();
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    for (int pass = 0; pass < 3; pass++) {
        for (size_t off = 0; off < file_size; off += sizeof(buf)) {
            if (pread(fd, buf, sizeof(buf), off) != (ssize_t)sizeof(buf)) {
                perror("pread");
                return 1;
            }
        }
    }
    close(fd);
    unsigned long long after = free_bytes();
    printf("arc-probe size=%zu used=%lld\n", file_size, (long long)before - (long long)after);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc == 2 ? argv[1] : "";
    if (strcmp(mode, "create") == 0) {
        if (!create_file()) {
            return 1;
        }
        printf("arc-probe created\n");
        return 0;
    }
    if (strcmp(mode, "read") == 0) {
        return read_file();
    }
    if (strcmp(mode, "delete") == 0) {
        // Truncate before the unlink: OSv's OpenZFS port does not yet free
        // the blocks of an unlinked file, and the test image would keep
        // losing the file's size on every run.
        if (truncate(path, 0) != 0 || unlink(path) != 0) {
            perror("unlink");
            return 1;
        }
        printf("arc-probe deleted\n");
        return 0;
    }
    fprintf(stderr, "usage: misc-zfs-arc-probe.so create|read|delete\n");
    return 2;
}
