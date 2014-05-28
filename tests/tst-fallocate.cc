/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <algorithm>
#define BUF_SIZE 4096UL

static int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

static void fill_file(int fd, unsigned long size)
{
    unsigned long to_write;
    char buf[BUF_SIZE];

    memset(buf, 0xAB, BUF_SIZE);
    to_write = size;
    while (to_write > 0) {
        long written_bytes = write(fd, buf, std::min(to_write, BUF_SIZE));
        assert(written_bytes >= 0);
        to_write -= written_bytes;
    }
}

int main(int argc, char *argv[])
{
    char path[64];
    int ret;
    struct stat st;
    struct statfs fs;

    strcpy(path, "/usr/tst-fallocateXXXXXX");
    mktemp(path);

    // Create a temporary file that's used in testing.
    auto fd = open(path, O_CREAT|O_TRUNC|O_RDWR, 0666);
    assert(fd > 0);

    ret = fallocate(fd, 0, 0, 0);
    report(ret == -1 && errno == EINVAL,
           "EINVAL when length is less than or equal to 0.");

    ret = fallocate(fd, 0, -1, 1);
    report(ret == -1 && errno == EINVAL,
           "EINVAL when offset is less than 0.");

    auto fd2 = open("/etc/mnttab", O_RDONLY);
    assert(fd2 > 0);
    ret = fallocate(fd2, 0, 4096, 4096);
    report(ret == -1 && errno == EBADF,
           "EBADF when fd isn't opened for writing.");
    close(fd2);

    // The purpose, of course, isn't fallocating a char device, but check
    // that the proper error will be returned when doing so.
    fd2 = open("/dev/random", O_RDWR);
    assert(fd2 > 0);
    ret = fallocate(fd2, 0, 4096, 4096);
    report(ret == -1 && errno == ENODEV,
           "ENODEV when fd doesn't refer a regular file nor a directory.");
    close(fd2);

    ret = fallocate(fd, FALLOC_FL_PUNCH_HOLE, 4096, 4096);
    report(ret == -1 && errno == ENOTSUP,
           "ENOTSUP when using FALLOC_FL_PUNCH_HOLE without FALLOC_FL_KEEP_SIZE.");

    fill_file(fd, 1024 * 1024);
    assert(fsync(fd) == 0);
    assert(fstat(fd, &st) == 0);
    assert(fstatfs(fd, &fs) == 0);
    long old_st_size = st.st_size;
    unsigned long old_f_bfree = fs.f_bfree;

    ret = fallocate(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE, 768*1024, 256*1024);
    report(ret == 0, "FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE");

    sync();
    memset(&st, 0, sizeof(st));
    memset(&fs, 0, sizeof(fs));
    assert(fstat(fd, &st) == 0);
    assert(fstatfs(fd, &fs) == 0);

    report(old_st_size == st.st_size, "Asserting that FALLOC_FL_KEEP_SIZE works!");
    // NOTE: ZFS uses variable-sized blocks, therefore any range could easily
    // overlap blocks.
    // As a result, it's very hard to determine how many blocks will be freed.
    // Anyway, let's only assume here that more blocks are available.
    report(old_f_bfree < fs.f_bfree, "Asserting that more blocks are available!");

#ifdef __OSV__
    // FALLOC_FL_KEEP_SIZE alone isn't supported by ZFS yet.
    ret = fallocate(fd, FALLOC_FL_KEEP_SIZE, 4096, 4096);
    report(ret == -1 && errno == EOPNOTSUPP,
           "EOPNOTSUPP; FALLOC_FL_KEEP_SIZE isn't supported yet.");
#endif
    close(fd);

    // Report results.
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
