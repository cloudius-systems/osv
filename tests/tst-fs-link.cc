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
#include <osv/vnode.h>
#include <osv/dentry.h>

static int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

extern "C" void drele(struct dentry *);
extern "C" int namei(char *, struct dentry **);

static int check_vnode_duplicity(void)
{
    int err = 0;
    char oldpath[] = "/usr/foo";
    char newpath[] = "/usr/foo2";
    struct dentry *olddp, *newdp;

    auto fd = open(oldpath, O_CREAT|O_TRUNC|O_RDWR, 0666);
    close(fd);

    if (link(oldpath, newpath) != 0) {
        unlink(oldpath);
        perror("link");
        return -1;
    }

    if (namei(oldpath, &olddp)) {
        err = -1;
        goto err;
    }

    if (namei(newpath, &newdp)) {
        err = -1;
        goto err1;
    }

    if (olddp->d_refcnt != 1 || newdp->d_refcnt != 1) {
        err = -1;
        goto err2;
    }

    vn_lock(olddp->d_vnode);
    if (olddp->d_vnode->v_refcnt != 2 || olddp->d_vnode != newdp->d_vnode) {
        err = -1;
    }
    vn_unlock(olddp->d_vnode);
 err2:
    drele(newdp);
 err1:
    drele(olddp);
 err:
    unlink(newpath);
    unlink(oldpath);
    return err;
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
	char oldp[64], newp[64];

	strcpy(oldp, "/usr/tst-fs-linkXXXXXX");
        mktemp(oldp);

	strcpy(newp, "/usr/tst-fs-linkXXXXXX");
        mktemp(newp);

        oldpath = oldp;
        newpath = newp;
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
    // vnode shouldn't be duplicated when looking up two hard links linked to the same inode
    report(check_vnode_duplicity() == 0, "don't duplicate in-memory vnodes");
    // Report results.
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
