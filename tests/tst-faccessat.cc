/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>

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
    // test absolute paths:

    remove("/tmp/f1");
    remove("/tmp/f2");
    expect(mknod("/tmp/f1", 0777|S_IFREG, 0), 0);
    expect(faccessat(-1, "/tmp/f1", F_OK, 0), 0);
    expect_errno(faccessat(-1, "/tmp/f2", F_OK, 0), ENOENT);

    // test paths relative to cwd:
    char* oldwd = getcwd(NULL, 0);
    chdir("/tmp");
    expect(faccessat(AT_FDCWD, "f1", F_OK, 0), 0);
    expect_errno(faccessat(AT_FDCWD, "f2", F_OK, 0), ENOENT);

    // test paths relative to open directory:
    chdir("/");
    int dir;
    expect((dir = open("/tmp", 0, O_DIRECTORY)) >= 0, true);
    expect(faccessat(dir, "f1", F_OK, 0), 0);
    expect_errno(faccessat(dir, "f2", F_OK, 0), ENOENT);
    close(dir);

    chdir(oldwd);
    free(oldwd);

    // test AT_SYMLINK_NOFOLLOW: the check applies to the symlink itself, not
    // its target. Before the fix this flag aborted the whole unikernel.
    remove("/tmp/f3");
    remove("/tmp/link_ok");
    remove("/tmp/link_dangling");
    expect(mknod("/tmp/f3", 0777|S_IFREG, 0), 0);
    expect(symlink("/tmp/f3", "/tmp/link_ok"), 0);
    expect(symlink("/tmp/does_not_exist", "/tmp/link_dangling"), 0);
    // Following the link reaches the real file; not following checks the link.
    expect(faccessat(-1, "/tmp/link_ok", F_OK, 0), 0);
    expect(faccessat(-1, "/tmp/link_ok", F_OK, AT_SYMLINK_NOFOLLOW), 0);
    // A dangling link: following fails ENOENT, not following still finds it.
    expect_errno(faccessat(-1, "/tmp/link_dangling", F_OK, 0), ENOENT);
    expect(faccessat(-1, "/tmp/link_dangling", F_OK, AT_SYMLINK_NOFOLLOW), 0);
    // A plain file with the flag falls through to the normal check.
    expect(faccessat(-1, "/tmp/f3", R_OK, AT_SYMLINK_NOFOLLOW), 0);
    expect_errno(faccessat(-1, "/tmp/f2", F_OK, AT_SYMLINK_NOFOLLOW), ENOENT);
    remove("/tmp/link_ok");
    remove("/tmp/link_dangling");
    remove("/tmp/f3");

    remove("/tmp/f1");

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
