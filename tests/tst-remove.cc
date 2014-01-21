/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <osv/debug.hh>

int tests = 0, fails = 0;

#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        debug("FAIL: %s:%d:  For %s expected %s, saw %s.\n", file, line, actuals, expecteds, actual);
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

    /********* test unlink() **************/
    // unlink() non-existant file returns ENOENT
    expect_errno(unlink("/tmp/tst-remove/f"), ENOENT);
    // unlink() normal file succeeds
    expect(mknod("/tmp/tst-remove/f", 0777|S_IFREG, 0), 0);
    expect(unlink("/tmp/tst-remove/f"), 0);
    // unlink() directory returns EISDIR on Linux (not EPERM as in Posix)
    expect(mkdir("/tmp/tst-remove/d", 0777), 0);
    expect_errno(unlink("/tmp/tst-remove/d"), EISDIR);

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

    /********* test remove() ***************/
    // TODO...


    // Finally remove the temporary directory (assumes the above left
    // nothing in it)
    expect(rmdir("/tmp/tst-remove"), 0);


    debug("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails == 0 ? 0 : 1;
}
