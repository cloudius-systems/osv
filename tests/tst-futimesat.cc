/*
 * Copyright (C) 2014 Jaspal Singh Dhillon
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <chrono>

static int tests = 0, fails = 0;
extern "C" int futimesat(int, const char *, const struct timeval[2]);

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

static void init_timeval(timeval &time, long tv_sec, long tv_usec)
{
    time.tv_sec = tv_sec;
    time.tv_usec = tv_usec;
}

int main(int argc, char *argv[])
{

    const char *tmp_folder = "/tmp";
    const char *tmp_folder_foo = "/tmp/tst_folder_foo";
    const char *tmp_file_bar = "/tmp/tst_folder_foo/bar";
    const char *rel_path_bar = "bar";
    const char *rel_path_bar_to_tmp = "tst_folder_foo/bar";
    timeval times[2];
    int ret;
    int fd;
    int dirfd;

    /* Create the temporary folder and file that are used in testing */
    report(mkdir(tmp_folder_foo, 0755) == 0, "create folder foo");

    fd = open(tmp_file_bar, O_CREAT|O_TRUNC|O_RDWR, 0666);
    report(fd > 0, "create file bar");
    write(fd, "test_bar" , 8);
    report(close(fd) == 0, "close file bar");

    /* Get fd to the foo directory */
    dirfd = open(tmp_folder_foo, O_RDONLY);
    report(dirfd > 0, "open folder foo");

    /* Initialize atime and mtime structures */
    init_timeval(times[0], 1234, 0); /* atime */
    init_timeval(times[1], 0, 1234); /* mtime */

    /* Test if futimesat ignores dirfd when path is absolute */
    ret = futimesat(100, tmp_file_bar, times);
    report(ret == 0, "check if futimesat worked successfully using absolute path!");

    /* Change directory to /tmp and check if futimesat can work with AT_FDCWD */
    report(chdir(tmp_folder) == 0, "change directory to tmp");
    ret = futimesat(AT_FDCWD, rel_path_bar_to_tmp, times);
    report(ret == 0, "check if futimesat worked successfully with AT_FDCWD!");

    /* Use dirfd and relative path of bar to check futimesat */
    ret = futimesat(dirfd, rel_path_bar, times);

    /* Force futimesat to fail using invalid dirfd */
    ret = futimesat(100, rel_path_bar, times);
    report(ret == -1 && errno == EBADF, "check if futimesat fails on invalid dirfd");

    /* Force futimesat to fail when dirfd points to a file */
    report(close(dirfd) == 0, "closing dirfd");
    dirfd = open(tmp_file_bar, O_RDONLY);
    report(dirfd > 0, "open file bar with dirfd");
    ret = futimesat(dirfd, rel_path_bar, times);
    report(ret == -1 && errno == ENOTDIR, "check if futimesat fails when dirfd points to file");

    // Clean up the temporary file and folder
    report(unlink(tmp_file_bar) == 0, "remove the file bar");
    report(rmdir(tmp_folder_foo) == 0, "remove the folder foo");

    // Report results.
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
