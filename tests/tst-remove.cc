/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This test can be run on either OSv or Linux. To compile for Linux, use
// c++ -std=c++11 tests/tst-remove.cc

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <iostream>

static int tests = 0, fails = 0;

#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals <<
                ", expected " << expecteds << ", saw " << actual << ".\n";
        return false;
    }
    return true;
}


#define expect_errno(call, experrno) ( \
        do_expect(call, -1, #call, "-1", __FILE__, __LINE__) && \
        do_expect(errno, experrno, #call " errno",  #experrno, __FILE__, __LINE__) )

int main(int argc, char **argv)
{
    expect(mkdir("/tmp/tst-remove", 0777), 0);
    auto tst_remove_dir = open("/tmp/tst-remove", O_DIRECTORY);
    expect(tst_remove_dir != -1, true);

    /********* test unlink() **************/
    // unlink() non-existant file returns ENOENT
    expect_errno(unlink("/tmp/tst-remove/f"), ENOENT);
    // unlink() normal file succeeds
    expect(mknod("/tmp/tst-remove/f", 0777|S_IFREG, 0), 0);
    expect(unlink("/tmp/tst-remove/f"), 0);
    // unlink() normal file succeeds while file is open
    int fd = open("/tmp/tst-remove/f", O_CREAT | O_RDWR, 0777);
    expect(unlink("/tmp/tst-remove/f"), 0);
    expect_errno(unlink("/tmp/tst-remove/f"), ENOENT);
    close(fd);
    // unlink() directory returns EISDIR on Linux (not EPERM as in Posix)
    expect(mkdir("/tmp/tst-remove/d", 0777), 0);
    expect_errno(unlink("/tmp/tst-remove/d"), EISDIR);
    // unlink() of an unwriteable file should succeed (it the permissions
    // of the parent directory which might matter, not those of the file
    // itself).
    expect(mknod("/tmp/tst-remove/f", 0777|S_IFREG, 0), 0);
    expect(chmod("/tmp/tst-remove/f", 0), 0);
    expect(unlink("/tmp/tst-remove/f"), 0);

    /********* test rmdir() ***************/
    // rmdir() of a non-empty directory returns ENOTEMPTY on Linux
    // (not EEXIST as in Posix)
    expect(mknod("/tmp/tst-remove/d/f", 0777|S_IFREG, 0), 0);
    expect_errno(rmdir("/tmp/tst-remove/d"), ENOTEMPTY);
    expect(unlink("/tmp/tst-remove/d/f"), 0);
    // rmdir() an empty directory succeeds
    expect(rmdir("/tmp/tst-remove/d"), 0);
    // rmdir() a non-existant directory returns ENOENT
    expect_errno(rmdir("/tmp/tst-remove/d"), ENOENT);
    // rmdir of regular file returns ENOTDIR:
    expect(mknod("/tmp/tst-remove/f", 0777|S_IFREG, 0), 0);
    expect_errno(rmdir("/tmp/tst-remove/f"), ENOTDIR);
    expect(unlink("/tmp/tst-remove/f"), 0);

    /********* test unlinkat() ***************/
    expect(mknod("/tmp/tst-remove/u", 0777|S_IFREG, 0), 0);
    expect(unlinkat(tst_remove_dir, "u", 0), 0);

    expect(mknod("/tmp/tst-remove/u2", 0777|S_IFREG, 0), 0);
    expect(chdir("/tmp/tst-remove"), 0);
    expect(unlinkat(AT_FDCWD, "u2", 0), 0);

    expect(mkdir("/tmp/tst-remove/ud", 0777), 0);
    expect(unlinkat(tst_remove_dir, "ud", AT_REMOVEDIR), 0);

    expect(mkdir("/tmp/tst-remove/ud2", 0777), 0);
    expect(chdir("/tmp/tst-remove"), 0);
    expect(unlinkat(AT_FDCWD, "ud2", AT_REMOVEDIR), 0);

    // Finally remove the temporary directory (assumes the above left
    // nothing in it)
    expect(close(tst_remove_dir), 0);
    expect(rmdir("/tmp/tst-remove"), 0);


    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
