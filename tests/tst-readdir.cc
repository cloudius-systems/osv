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

#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <string>

#if defined(READ_ONLY_FS)
#define SUBDIR "rofs"
#else
#define SUBDIR "tmp"
#endif

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

int main(int argc, char **argv)
{
    printf("Testing readdir() and related functions\n");
#if defined(READ_ONLY_FS)
    report(mkdir("/rofs/tst-readdir-empty2", 0777) == -1 && errno == EROFS, "mkdir");
#else
    report(mkdir("/tmp/tst-readdir-empty", 0777) == 0, "mkdir empty");
#endif

    // test readdir() on empty directory
    DIR *dir = opendir("/" SUBDIR "/tst-readdir-empty");
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
#if defined(READ_ONLY_FS)
    report(creat("/rofs/tst-readdir/non-existent", 0777) < 0 && errno == EROFS, "creat");
#else
    report(mkdir("/tmp/tst-readdir", 0777) == 0, "mkdir empty");
    int fd=creat("/tmp/tst-readdir/aaa", 0777);
    report(fd>=0, "creat");
    close(fd);
#endif
    dir = opendir("/" SUBDIR "/tst-readdir");
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
    dir = opendir("/" SUBDIR "/tst-readdir");
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

    // test rewinddir(), still on a directory with one file
    dir = opendir("/" SUBDIR "/tst-readdir");
    report(dir != NULL, "opendir");
    ent = (struct dirent *)malloc(4096);
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, "."), "first entry is .");
    rewinddir(dir);
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, "."), "first entry is .");
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, ".."), "second entry is ..");
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, "aaa"), "third entry is aaa");
    report(readdir_r(dir, ent, &r)==0 && r==NULL, "no more entries");
    rewinddir(dir);
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

    // test telldir(), seekdir()
    dir = opendir("/" SUBDIR "/tst-readdir");
    report(dir != NULL, "opendir");
    ent = (struct dirent *)malloc(4096);
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, "."), "first entry is .");
    auto p = telldir(dir);
    report(p >= 0, "telldir");
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, ".."), "second entry is ..");
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, "aaa"), "third entry is aaa");
    report(readdir_r(dir, ent, &r)==0 && r==NULL, "no more entries");
    seekdir(dir,p);
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, ".."), "second entry is ..");
    report(readdir_r(dir, ent, &r)==0 && r!=NULL, "readdir_r");
    report(!strcmp(ent->d_name, "aaa"), "third entry is aaa");
    report(readdir_r(dir, ent, &r)==0 && r==NULL, "no more entries");
    iret = closedir(dir);
    report(iret == 0, "closedir");
    free(ent);

#if defined(READ_ONLY_FS)
    report(unlink("/rofs/tst-readdir/aaa")==-1 && errno == EROFS, "unlink aaa");
    report(rmdir("/rofs/tst-readdir")==-1 && errno == ENOTEMPTY, "rmdir");
    report(mknod("/rofs/tst-readdir/b", 0777|S_IFREG, 0) == -1 && errno == EROFS, "mknod");
#else
    // clean up the temporary directory we created with one file.
    report(unlink("/tmp/tst-readdir/aaa")==0, "unlink aaa");
    report(rmdir("/tmp/tst-readdir")==0, "rmdir");


    // test removal of all a directory's nodes
    report(mkdir("/tmp/tst-readdir", 0777) == 0, "mkdir");
    report(mkdir("/tmp/tst-readdir/a", 0777) == 0, "mkdir");
    report(mkdir("/tmp/tst-readdir/b", 0777) == 0, "mkdir");
    fd=creat("/tmp/tst-readdir/c", 0777);
    report(fd>=0, "creat");
    close(fd);
    report(mkdir("/tmp/tst-readdir/d", 0777) == 0, "mkdir");
    fd=creat("/tmp/tst-readdir/e", 0777);
    report(fd>=0, "creat");
    close(fd);
    // Note: Linux normally returns ENOTEMPTY when deleting a non-empty
    // directory, OSv returns EEXIST, which Posix also allows.
    report(rmdir("/tmp/tst-readdir") == -1 && (errno == ENOTEMPTY||errno == EEXIST), "can't rmdir non-empty directory");
    dir = opendir("/tmp/tst-readdir");
    report(dir != NULL, "opendir");
    while ((ent = readdir(dir)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;
        std::string path("/tmp/tst-readdir");
        path += "/";
        path += ent->d_name;
        iret = remove(path.c_str());
        report(iret == 0, "remove");
    }
    iret = closedir(dir);
    report(iret == 0, "closedir");
    report(rmdir("/tmp/tst-readdir")==0, "rmdir");

    // Test scandir() with alphasort() on a directory with several files
    report(mkdir("/tmp/tst-readdir", 0777) == 0, "mkdir");
    report(mknod("/tmp/tst-readdir/b", 0777|S_IFREG, 0) == 0, "mknod");
    report(mknod("/tmp/tst-readdir/a", 0777|S_IFREG, 0) == 0, "mknod");
    report(mknod("/tmp/tst-readdir/d", 0777|S_IFREG, 0) == 0, "mknod");
    report(mknod("/tmp/tst-readdir/c", 0777|S_IFREG, 0) == 0, "mknod");
    struct dirent **namelist;
    int count = scandir("/tmp/tst-readdir", &namelist, NULL, alphasort);
    report(count == 6, "scandir return 4 entries");
    printf("count = %d\n", count);
    for (int i = 0; i < count; i++) {
        printf("namelist[%d] = %s\n", i, namelist[i]->d_name);
    }
    report(count > 0 && !strcmp(namelist[0]->d_name,"."), "scandir return .");
    report(count > 1 && !strcmp(namelist[1]->d_name,".."), "scandir return ..");
    report(count > 2 && !strcmp(namelist[2]->d_name,"a"), "scandir return a");
    report(count > 3 && !strcmp(namelist[3]->d_name,"b"), "scandir return b");
    report(count > 4 && !strcmp(namelist[4]->d_name,"c"), "scandir return c");
    report(count > 5 && !strcmp(namelist[5]->d_name,"d"), "scandir return d");
    for (int i = 0; i < count; i++) {
        free(namelist[i]);
    }
    if (count >= 0) {
        free(namelist);
    }
    report(unlink("/tmp/tst-readdir/a")==0, "unlink");
    report(unlink("/tmp/tst-readdir/b")==0, "unlink");
    report(unlink("/tmp/tst-readdir/c")==0, "unlink");
    report(unlink("/tmp/tst-readdir/d")==0, "unlink");
    report(rmdir("/tmp/tst-readdir")==0, "rmdir");
    report(rmdir("/tmp/tst-readdir-empty")==0, "rmdir empty");
#endif

    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails == 0 ? 0 : 1;
}


