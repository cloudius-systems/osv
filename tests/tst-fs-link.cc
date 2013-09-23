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
    struct stat st[2];
    char buf[4] = { 0 };
    char buf2[4] = { 0 };

    // Create a temporary file that's used in testing.
    auto fd = open("/usr/foo", O_CREAT|O_TRUNC|O_RDWR, 0666);
    write(fd, "test", 4);
    lseek(fd, 0, SEEK_SET);
    read(fd, buf, 4);
    report(fd > 0, "create a file");
    report(close(fd) == 0, "close the file");

    // Create a hard link
    report(link("/usr/foo", "/usr/foo2") == 0, "create a hard link");

    auto fd2 = open("/usr/foo2", O_RDONLY);
    lseek(fd2, 0, SEEK_SET);
    read(fd2, buf2, 4);
    close(fd2);

    // Check that it's possible to read the content from another link
    // of the same inode.
    report(strncmp(buf, buf2, 4) == 0, "read content from another link");
    // Check that hard link count is increased
    report(stat("/usr/foo", &st[0]) == 0, "stat the file");
    report(st[0].st_nlink == 2, "hard link count is increased");
    // Check that hard link points to the same inode
    report(stat("/usr/foo2", &st[1]) == 0, "stat the hard link");
    report(st[0].st_dev == st[1].st_dev, "stat device IDs match");
    report(st[0].st_ino == st[1].st_ino, "stat inode numbers match");
    // Remove the hard link
    report(unlink("/usr/foo2") == 0, "remove the hard link");
    // Check that hard link count is decreased
    report(stat("/usr/foo", &st[0]) == 0, "stat the file");
    report(st[0].st_nlink == 1, "hard link count is decreased");
    // Clean up the temporary file.
    report(unlink("/usr/foo") == 0, "remove the file");
    // Report results.
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
