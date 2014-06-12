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

using namespace std::chrono;

static int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

/* Convert time_t to system_clock::timepoint */
system_clock::time_point to_timeptr(const time_t &time)
{
    return system_clock::from_time_t(time);
}

/* Convert timespec to system_clock::timepoint */
system_clock::time_point to_timeptr(const timespec &time)
{
    return system_clock::from_time_t(time.tv_sec) + nanoseconds(time.tv_nsec);
}

/*
 * Convert everything to system_clock::timepoint before the comparison.
 */
static bool compare_time(const timespec &time1, const timespec &time2)
{
    return to_timeptr(time1) == to_timeptr(time2);
}

static void init_timespec(timespec &time, time_t tv_sec, long tv_nsec)
{
    time.tv_sec = tv_sec;
    time.tv_nsec = tv_nsec;
}

int main(int argc, char *argv[])
{
    const char *tmp_folder = "/tmp";
    const char *tmp_folder_foo = "/tmp/tst_folder_foo";
    const char *tmp_file_bar = "/tmp/tst_folder_foo/bar";
    const char *rel_path_bar_to_foo = "bar";
    const char *rel_path_bar_to_tmp = "tst_folder_foo/bar";
    struct timespec times[2];
    int ret;
    int fd;
    int dirfd;
    struct stat st;

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
    init_timespec(times[0], 1234, 0); /* atime */
    init_timespec(times[1], 0, 1234); /* mtime */

    /* Test if utimensat ignores dirfd when path is absolute */
    ret = utimensat(100, tmp_file_bar, times, 0);
    report(ret == 0, "utimensat worked successfully using absolute path");

    /* Stat the file to check if utimensat made our time changes
     * to the inode */
    report(stat(tmp_file_bar, &st) == 0, "stat the file");

    /* Check atime changes */
    report(compare_time(st.st_atim, times[0]), "check atime changes");

    /* Check mtime changes */
    report(compare_time(st.st_mtim, times[1]), "check mtime changes");

    /* Change directory to /tmp and check if utimensat can work with AT_FDCWD */
    report(chdir(tmp_folder) == 0, "change directory to tmp");
    ret = utimensat(AT_FDCWD, rel_path_bar_to_tmp, times, 0);
    report(ret == 0, "utimensat worked successfully with AT_FDCWD");

     /* Use dirfd and relative path of bar to check utimensat */
     ret = utimensat(dirfd, rel_path_bar_to_foo, times, 0);
     report(ret == 0, "utimensat works with dirfd and relative path");

    /* Force utimensat to fail using invalid dirfd */
    ret = utimensat(100, rel_path_bar_to_foo, times, 0);
    report(ret == -1 && errno == EBADF, "utimensat fails with invalid dirfd");

    /* Check utimensat when times == NULL */
    ret = utimensat(dirfd, rel_path_bar_to_foo, NULL, 0);
    report(ret == 0, "utimensat works with times == NULL");

    /* Force utimensat to fail when dirfd was AT_FDCWD and pathname is NULL */
    ret = utimensat(AT_FDCWD, NULL, times, 0);
    report(ret == -1 && errno == EFAULT, "utimensat fails when dirfd is AT_FDCWD and pathname is NULL");

    /* Force utimensat to fail with invalid flags */
    ret = utimensat(dirfd, rel_path_bar_to_tmp, times, 23);
    report(ret == -1 && errno == EINVAL, "utimensat fails with invalid flags");

    /* Force utimensat to fail with invalid values in times */
    init_timespec(times[0], -1, 100); /* change atime */
    ret = utimensat(dirfd, rel_path_bar_to_tmp, times, 0);
    report(ret == -1 && errno == EINVAL, "utimensat fails with invalid value in times");

    init_timespec(times[0], 1234, 100);

    /* Force utimensat to fail with non-existent path */
    ret = utimensat(dirfd, "this/does/not/exist", times, 0);
    report(ret == -1 && errno == ENOENT, "utimensat fails with non-existent path");

    /* Force utimensat to fail with empty pathname */
    ret = utimensat(dirfd, "", times, 0);
    report(ret == -1 && errno == ENOENT, "utimensat fails with empty pathname");

    /* Force utimensat with invalid dirfd and relative pathname */
    report(close(dirfd) == 0, "closing dirfd");
    dirfd = open(tmp_file_bar, O_RDONLY);
    report(dirfd > 0, "open file bar with dirfd");
    ret = utimensat(dirfd, rel_path_bar_to_foo, times, 0);
    report(ret == -1 && errno == ENOTDIR, "utimensat fails when dirfd points to file and relative pathname is given");

    /* Clean up temporary files */
     report(unlink(tmp_file_bar) == 0, "remove the file bar");
     report(rmdir(tmp_folder_foo) == 0, "remove the folder foo");

    /* Report results. */
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
