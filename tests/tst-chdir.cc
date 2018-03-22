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
#include <errno.h>

#include <iostream>

#if defined(READ_ONLY_FS)
#define SUBDIR "rofs"
#else
#define SUBDIR "tmp"
#endif

int tests = 0, fails = 0;

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
#if defined(READ_ONLY_FS)
    expect_errno(mkdir("/rofs/tst-chdir-non-existent", 0777), EROFS);
#else
    expect(mkdir("/tmp/tst-chdir", 0777), 0);
#endif
    /********* test chdir() **************/
    // chdir to non-existant subdirectory returns ENOENT:
    expect_errno(chdir("/" SUBDIR "/tst-chdir/x"), ENOENT);
    // chdir to path with non-existant middle component returns ENOTDIR:
    expect_errno(chdir("/" SUBDIR " /tst-chdir/x/y"), ENOENT);
    // chdir to a non-directory file, or if a non-directory is part of the
    // path, fails with ENOTDIR:
#if defined(READ_ONLY_FS)
    expect_errno(mknod("/rofs/tst-chdir/f-nonexistent", 0777|S_IFREG, 0), EROFS);
#else
    expect(mknod("/tmp/tst-chdir/f", 0777|S_IFREG, 0), 0);
#endif
    expect_errno(chdir("/" SUBDIR "/tst-chdir/f"), ENOTDIR);
    expect_errno(chdir("/" SUBDIR "/tst-chdir/f/g"), ENOTDIR);
#if defined(READ_ONLY_FS)
    expect_errno(unlink("/rofs/tst-chdir/f"), EROFS);
#else
    expect(unlink("/tmp/tst-chdir/f"), 0);
#endif
    // chdir to existing directory succeeds:
    expect(chdir("/"), 0);
    expect(chdir("/" SUBDIR), 0);
    expect(chdir("/" SUBDIR "/tst-chdir"), 0);
    expect(chdir("/" SUBDIR "/tst-chdir/"), 0);
    expect(chdir("/"), 0);
    // chdir actually "works" (changes the return value of getcwd)
    char buf[1024];
    expect(chdir("/" SUBDIR "/tst-chdir"), 0);
    getcwd(buf, sizeof(buf));
    expect(strcmp(buf, "/" SUBDIR "/tst-chdir"), 0);
    expect(chdir("/"), 0);

#if defined(READ_ONLY_FS)
    expect_errno(rmdir("/rofs/tst-chdir"), ENOTEMPTY);
#else
    // Finally remove the temporary directory (assumes the above left
    // nothing in it)
    expect(rmdir("/tmp/tst-chdir"), 0);
#endif

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
