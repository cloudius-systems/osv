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

#include <debug.hh>

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    debug("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

int main(int argc, char **argv)
{
    debug("Testing readdir() and related functions\n");
    report(mkdir("/tmp/tst-readdir", 0777) == 0, "mkdir");

    // test readdir() on empty directory
    DIR *dir = opendir("/tmp/tst-readdir");
    report(dir != NULL, "opendir");
    struct dirent *ent;
    ent = readdir(dir);
    report(ent != NULL, "readdir first entry");
    report(!strcmp(ent->d_name, "."), "first directory entry is .");
    ent = readdir(dir);
    report(ent != NULL, "readdir second entry");
    report(!strcmp(ent->d_name, ".."), "second directory entry is ..");
    ent = readdir(dir);
    report(!ent, "no third directory entry");
    int iret = closedir(dir);
    report(iret == 0, "closedir");

    // test readdir() on directory with one file
    int fd;
    fd=creat("/tmp/tst-readdir/aaa", 0777);
    report(fd>=0, "creat");
    close(fd);
    dir = opendir("/tmp/tst-readdir");
    report(dir != NULL, "opendir");
    ent = readdir(dir);
    report(ent != NULL, "readdir");
    report(!strcmp(ent->d_name, "."), "first entry is .");
    ent = readdir(dir);
    report(ent != NULL, "readdir");
    report(!strcmp(ent->d_name, ".."), "second entry is ..");
    ent = readdir(dir);
    report(ent != NULL, "readdir");
    report(!strcmp(ent->d_name, "aaa"), "third entry is aaa");
    ent = readdir(dir);
    report(!ent, "no more entries");
    iret = closedir(dir);
    report(iret == 0, "closedir");

    // test readdir_r() on directory with one file
    dir = opendir("/tmp/tst-readdir");
    report(dir != NULL, "opendir");
    ent = (struct dirent *)malloc(4096);
    struct dirent *r;
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, "."), "first entry is .");
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, ".."), "second entry is ..");
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, "aaa"), "third entry is aaa");
    report(readdir_r(dir, ent, &r)==0 && r==NULL, "no more entries");
    iret = closedir(dir);
    report(iret == 0, "closedir");
    free(ent);

    report(unlink("/tmp/tst-readdir/aaa")==0, "unlink aaa");
    report(rmdir("/tmp/tst-readdir")==0, "rmdir");

    debug("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails == 0 ? 0 : 1;
}


