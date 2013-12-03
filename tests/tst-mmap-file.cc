/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>

static int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

static int write_pattern(int fd, size_t size, unsigned char pattern, int mmap_flags, size_t offset)
{
    auto* p = reinterpret_cast<unsigned char*>(mmap(NULL, size, PROT_READ|PROT_WRITE, mmap_flags, fd, offset));
    if (p == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    memset(p, pattern, size);
    if (munmap(p, size) < 0) {
        perror("munmap");
        return -1;
    }
    return 0;
}

static int verify_pattern(int fd, size_t size, unsigned char pattern, int mmap_flags, size_t offset)
{
    auto* p = reinterpret_cast<unsigned char*>(mmap(NULL, size, PROT_READ|PROT_WRITE, mmap_flags, fd, offset));
    if (p == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    for (size_t i = 0; i < size; i++) {
        if (p[i] != pattern) {
            printf("pattern didn't match\n");
            return -1;
        }
    }
    if (munmap(p, size) < 0) {
        perror("munmap");
        return -1;
    }
    return 0;
}

static int check_mapping(void *addr, size_t size, unsigned flags, int fd,
                         size_t offset, int expected_errno)
{
    void *ret = mmap(addr, size, PROT_READ|PROT_WRITE, flags, fd, offset);

    if (ret == MAP_FAILED && errno == expected_errno) {
        return 0;
    }

    if (ret != addr && ret == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    if (munmap(ret, size) < 0) {
        perror("munmap");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    auto fd = open("/tmp/mmap-file-test", O_CREAT|O_TRUNC|O_RDWR, 0666);
    report(fd > 0, "open");
    constexpr int size = 8192;
    report(ftruncate(fd, size) == 0, "ftruncate");
    report(write_pattern(fd, size, 0xfe, MAP_SHARED, 0) == 0, "write pattern to MAP_SHARED");
    report(verify_pattern(fd, size, 0xfe, MAP_PRIVATE, 0) == 0, "verify pattern was written to file");
    report(write_pattern(fd, size, 0x0f, MAP_PRIVATE, 0) == 0, "write pattern to MAP_PRIVATE");
    report(verify_pattern(fd, size, 0xfe, MAP_PRIVATE, 0) == 0, "verify pattern didn't change");
    report(write_pattern(fd, size/2, 0x0f, MAP_SHARED, size/2) == 0, "write pattern to partial MAP_SHARED");
    report(verify_pattern(fd, size/2, 0xfe, MAP_PRIVATE, 0) == 0, "verify pattern didn't change in unmapped part");
    report(verify_pattern(fd, size/2, 0x0f, MAP_PRIVATE, size/2) == 0, "verify pattern changed in mapped part");

    report(check_mapping(NULL, size, MAP_SHARED | MAP_PRIVATE, fd, 0, EINVAL) == 0,
        "force EINVAL by not passing neither MAP_PRIVATE nor MAP_SHARED.");
    report(check_mapping(NULL, size, 0, fd, 0, EINVAL) == 0,
        "force EINVAL by passing both MAP_PRIVATE and MAP_SHARED.");
    // if MAP_ANONYMOUS isn't set, then we must check the validity of the fd.
    report(check_mapping(NULL, size, MAP_SHARED, -1, 0, EBADF) == 0,
        "force EBADF by passing an invalid file descriptor.");
    report(check_mapping(NULL, size, MAP_SHARED, fd, 4095, EINVAL) == 0,
        "force EINVAL by passing an unaligned offset.");
    report(check_mapping(NULL, 0, MAP_SHARED, fd, 0, EINVAL) == 0,
        "force EINVAL by passing length equals to zero.");

    constexpr void *unaligned_addr = reinterpret_cast<void*>(0xefff1001); // unaligned 4k-sized page.
    constexpr void *aligned_addr   = reinterpret_cast<void*>(0xefff1000); // aligned 4k-sized page.
    // if MAP_FIXED was specified, then mmap should return EINVAL to unaligned addresses.
    report(check_mapping(unaligned_addr, size, MAP_SHARED | MAP_FIXED, fd, 0, EINVAL) == 0,
        "force EINVAL by passing an unaligned addr and the flag MAP_FIXED.");
    // if MAP_FIXED was specified and an aligned addr was passed, then mmap should proceed correctly.
    report(check_mapping(aligned_addr, size, MAP_SHARED | MAP_FIXED, fd, 0, 0) == 0,
        "passed an aligned addr and MAP_FIXED, then mmap should return exactly the specified addr.");

    report(close(fd) == 0, "close");

    fd = open("/tmp/mmap-file-test", O_WRONLY);
    report(fd > 0, "open file again: O_WRONLY");
    report(check_mapping(NULL, size, MAP_PRIVATE, fd, 0, EACCES) == 0,
        "force EACCES by private mapping a file that is not opened for reading");
    report(close(fd) == 0, "close again");

    fd = open("/tmp/mmap-file-test", O_WRONLY);
    report(fd > 0, "open file again: O_WRONLY");
    report(check_mapping(NULL, size, MAP_SHARED, fd, 0, EACCES) == 0,
        "force EACCES by shared mapping a file that is not read-write (and PROT_WRITE is set)");
    report(close(fd) == 0, "close again");

    // TODO: map an append-only file with prot asking for PROT_WRITE, mmap should return EACCES.
    // TODO: map a file under a fs mounted with the flag NO_EXEC and prot asked for PROT_EXEC (expect EPERM).

    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
