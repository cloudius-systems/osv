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
    struct stat st;

    // test absolute paths:

    remove("/tmp/f1");
    remove("/tmp/f2");
    expect(mknod("/tmp/f1", 0777|S_IFREG, 0), 0);
    st.st_size = 123;
    expect(fstatat(-1, "/tmp/f1", &st, 0), 0);
    expect(st.st_size, (off_t)0);
    expect_errno(fstatat(-1, "/tmp/f2", &st, 0), ENOENT);
    expect_errno(fstatat(-1, "/tmp/f2", &st, AT_SYMLINK_NOFOLLOW), ENOENT);

    // test paths relative to cwd:
    char* oldwd = getcwd(NULL, 0);
    chdir("/tmp");
    st.st_size = 123;
    expect(fstatat(AT_FDCWD, "f1", &st, 0), 0);
    expect(st.st_size, (off_t)0);
    expect_errno(fstatat(AT_FDCWD, "f2", &st, 0), ENOENT);
    expect_errno(fstatat(AT_FDCWD, "f2", &st, AT_SYMLINK_NOFOLLOW), ENOENT);

    // test paths relative to open directory:
    chdir("/");
    int dir;
    expect((dir = open("/tmp", 0, O_DIRECTORY)) >= 0, true);
    st.st_size = 123;
    expect(fstatat(dir, "f1", &st, 0), 0);
    expect(st.st_size, (off_t)0);
    expect_errno(fstatat(dir, "f2", &st, 0), ENOENT);
    expect_errno(fstatat(dir, "f2", &st, AT_SYMLINK_NOFOLLOW), ENOENT);
    close(dir);

    // test operating on an open file itself (not a directory)
    expect((dir = open("/tmp/f1", 0, O_PATH)) >= 0, true);
    st.st_size = 123;
    expect(fstatat(dir, "", &st, AT_EMPTY_PATH), 0);
    expect(st.st_size, (off_t)0);

    // test AT_SYMLINK_NOFOLLOW with actual symlink
    expect(symlink("/tmp/f1", "/tmp/symlink"), 0);
    expect((dir = open("/tmp", 0, O_DIRECTORY)) >= 0, true);
    expect(fstatat(dir, "symlink", &st, AT_SYMLINK_NOFOLLOW), 0);
    expect(S_ISLNK(st.st_mode) != 0, true);

    close(dir);
    chdir(oldwd);
    free(oldwd);
    remove("/tmp/symlink");
    remove("/tmp/f1");

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
