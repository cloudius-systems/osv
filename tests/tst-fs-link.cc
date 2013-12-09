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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

static int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

int main(int argc, char *argv[])
{
    const char *oldpath, *newpath;
    struct stat st[2];
    char buf[4] = { 0 };
    char buf2[4] = { 0 };

    if (argc > 2) {
        oldpath = argv[1];
        newpath = argv[2];
    } else {
        oldpath = "/usr/foo";
        newpath = "/usr/foo2";
    }

    report(link(oldpath, newpath) < 0 && errno == ENOENT, "link returns ENOENT if source path does not exists");

    // Create a temporary file that's used in testing.
    auto fd = open(oldpath, O_CREAT|O_TRUNC|O_RDWR, 0666);
    write(fd, "test", 4);
    lseek(fd, 0, SEEK_SET);
    read(fd, buf, 4);
    report(fd > 0, "create a file");
    report(close(fd) == 0, "close the file");

    // Create a hard link
    report(link(oldpath, newpath) == 0, "create a hard link");

    report(link(oldpath, newpath) < 0 && errno == EEXIST, "link returns EEXIST if destination path exists");

    auto fd2 = open(newpath, O_RDONLY);
    lseek(fd2, 0, SEEK_SET);
    read(fd2, buf2, 4);
    close(fd2);

    // Check that it's possible to read the content from another link
    // of the same inode.
    report(strncmp(buf, buf2, 4) == 0, "read content from another link");
    // Check that hard link count is increased
    report(stat(oldpath, &st[0]) == 0, "stat the file");
    report(st[0].st_nlink == 2, "hard link count is increased");
    // Check that hard link points to the same inode
    report(stat(newpath, &st[1]) == 0, "stat the hard link");
    report(st[0].st_dev == st[1].st_dev, "stat device IDs match");
    report(st[0].st_ino == st[1].st_ino, "stat inode numbers match");
    // Remove the hard link
    report(unlink(newpath) == 0, "remove the hard link");
    // Check that hard link count is decreased
    report(stat(oldpath, &st[0]) == 0, "stat the file");
    report(st[0].st_nlink == 1, "hard link count is decreased");
    // Clean up the temporary file.
    report(unlink(oldpath) == 0, "remove the file");
    // Report results.
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
