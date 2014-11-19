/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>
#include <dirent.h>

int main(int argc, char *argv[])
{
    if (argc > 1) {
        fprintf(stderr, "ls: no arguments implemented yet.\n");
        return 1;
    }

    DIR *dir = opendir(".");
    if (dir == NULL) {
        perror("ls");
        return 2;
    }

    struct dirent *entry;
    int retval;
    errno = 0;

    while ((entry = readdir(dir)) != NULL) {
        fprintf(stdout, "%s\n", entry->d_name);
    }

    if (errno != 0) {
        perror("ls");
        retval = 3;
        goto out;
    }

    retval = 0;
 out:
    closedir(dir);
    return retval;
}
