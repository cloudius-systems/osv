/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
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
extern "C" int utimes(const char *, const struct timeval[2]);
extern "C" int lutimes(const char *, const struct timeval[2]);

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

using namespace std::chrono;

/* Convert timeval to system_clock::timepoint */
system_clock::time_point to_timeptr(const timeval &time)
{
    return system_clock::from_time_t(time.tv_sec) + microseconds(time.tv_usec);
}

/* Convert timespec to system_clock::timepoint */
system_clock::time_point to_timeptr(const timespec &time)
{
    return system_clock::from_time_t(time.tv_sec) + nanoseconds(time.tv_nsec);
}

/*
 * Convert everything to system_clock::timepoint before the comparison.
 * Need to take into account that the tv_usec (microseconds) field from
 * timeval struct will be converted into nanoseconds.
 */
static bool compare_time(const timeval &time1, const timespec &time2)
{
    return to_timeptr(time1) == to_timeptr(time2);
}

static void init_timeval(timeval &time, long tv_sec, long tv_usec)
{
    time.tv_sec = tv_sec;
    time.tv_usec = tv_usec;
}

int main(int argc, char *argv[])
{
    const char *path;
    char *pathlnk;
    struct stat st;
    timeval times[2], timesl[2];
    int ret;

    if (argc > 1) {
        path = argv[1];
    } else {
        path = "/tmp/foo";
    }
    asprintf(&pathlnk, "%s.lnk", path);

    // Create a temporary file that's used in testing.
    auto fd = open(path, O_CREAT|O_TRUNC|O_RDWR, 0666);
    report(fd > 0, "create a file");
    write(fd, "test", 4);
    report(close(fd) == 0, "close the file");
    // Create a link. utimes should follow it, lutimes should not
    report(symlink(path, pathlnk) == 0, "create a link");

    report(stat(path, &st) == 0, "stat the file");

    /* Dump current atime and mtime */
    printf("ino: %lu - old -> st_atim: %ld:%ld - st_mtim: %ld:%ld\n",
        st.st_ino,
        st.st_atim.tv_sec, st.st_atim.tv_nsec,
        st.st_mtim.tv_sec, st.st_mtim.tv_nsec);

    /* Initialize atime and mtime structures */
    init_timeval(times[0], 1234, 0); /* atime */
    init_timeval(times[1], 0, 1234); /* mtime */
    init_timeval(timesl[0], 54321, 0); /* atime link */
    init_timeval(timesl[1], 0, 54321); /* mtime link */

    ret = utimes(path, times);
    report(ret == 0, "check if utimes worked successfully!");
    printf("utimes return: %d\n", utimes(path, times));

    /* Stat the file again to see if utimes made our time changes
     * to the inode */
    report(stat(path, &st) == 0, "stat the file again");

    /* Check atime changes */
    report(compare_time(times[0], st.st_atim),
        "check changes made to atime");

    /* Check mtime changes */
    report(compare_time(times[1], st.st_mtim),
        "check changes made to mtime");

    /* Dump the changes to atime and mtime */
    printf("ino: %lu - new -> st_atim: %ld:%ld - st_mtim: %ld:%ld\n",
        st.st_ino,
        st.st_atim.tv_sec, st.st_atim.tv_nsec,
        st.st_mtim.tv_sec, st.st_mtim.tv_nsec);

    ret = lutimes(pathlnk, timesl);
    report(ret == 0, "check if lutimes worked successfully!");

    report(compare_time(times[0], st.st_atim),
        "check if lutimes made no changes to atime");
    report(compare_time(times[1], st.st_mtim),
        "check if lutimes made no changes to mtime");

    report(lstat(pathlnk, &st) == 0, "stat the link");
    report(compare_time(timesl[0], st.st_atim),
        "check if lutimes changes to link's atime");
    report(compare_time(timesl[1], st.st_mtim),
        "check if lutimes made changes to link's mtime");

    ret = utimes(pathlnk, timesl);
    report(ret == 0, "check if utimes followed the link!");
    report(stat(path, &st) == 0, "stat the file again");
    report(compare_time(timesl[0], st.st_atim),
        "check if utimes changes to link target's atime");
    report(compare_time(timesl[1], st.st_mtim),
        "check if utimes made changes to link target's mtime");


#ifndef LINUX
    /* Force utimes to fail */
    times[0].tv_sec = -1;
    ret = utimes(path, times);
    report(ret == -1 && errno == EINVAL,
        "check if utimes failed as desired!");
#endif

    /* Check if utimes works with NULL as argument */
    ret = utimes(path, NULL);
    report(ret == 0, "utimes works with NULL as argument");

    // Clean up the temporary files.
    report(unlink(pathlnk) == 0, "remove the link");
    report(unlink(path) == 0, "remove the file");

    // Report results.
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    free(pathlnk);
    return fails == 0;
}
