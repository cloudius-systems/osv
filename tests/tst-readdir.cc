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

int main(int argc, char **argv)
{
    debug("Testing readdir() and related functions\n");
    assert(mkdir("/tmp/tst-readdir", 0777) == 0);

    // test readdir() on empty directory
    DIR *dir = opendir("/tmp/tst-readdir");
    assert(dir != NULL);
    struct dirent *ent;
    ent = readdir(dir);
    assert(ent != NULL);
    assert(!strcmp(ent->d_name, "."));
    ent = readdir(dir);
    assert(ent != NULL);
    assert(!strcmp(ent->d_name, ".."));
    ent = readdir(dir);
    assert(!ent);
    assert(closedir(dir) == 0);

    // test readdir() on directory with one file
    int fd;
    fd=creat("/tmp/tst-readdir/aaa", 0777);
    assert(fd>=0);
    close(fd);
    dir = opendir("/tmp/tst-readdir");
    assert(dir != NULL);
    ent = readdir(dir);
    assert(ent != NULL);
    assert(!strcmp(ent->d_name, "."));
    ent = readdir(dir);
    assert(ent != NULL);
    assert(!strcmp(ent->d_name, ".."));
    ent = readdir(dir);
    assert(ent != NULL);
    assert(!strcmp(ent->d_name, "aaa"));
    ent = readdir(dir);
    assert(!ent);
    assert(closedir(dir) == 0);

    // test readdir_r() on directory with one file
    dir = opendir("/tmp/tst-readdir");
    assert(dir != NULL);
    ent = (struct dirent *)malloc(4096);
    struct dirent *r;
    assert(readdir_r(dir, ent, &r)==0 && r!=NULL);
    assert(!strcmp(ent->d_name, "."));
    assert(readdir_r(dir, ent, &r)==0 && r!=NULL);
    assert(!strcmp(ent->d_name, ".."));
    assert(readdir_r(dir, ent, &r)==0 && r!=NULL);
    assert(!strcmp(ent->d_name, "aaa"));
    assert(readdir_r(dir, ent, &r)==0 && r==NULL);
    assert(closedir(dir) == 0);
    free(ent);

    assert(unlink("/tmp/tst-readdir/aaa")==0);
    assert(rmdir("/tmp/tst-readdir")==0);


    debug("Tested readdir() successfully\n");
    return 0;
}


