/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include "kobj.h"

extern "C" struct _buf *
kobj_open_file(const char *file)
{
    struct _buf *out;
    int fd;

    if (!(out = (struct _buf *) malloc(sizeof(*out)))) {
        return (struct _buf *)-1;
    }
    fd = open(file, O_NOFOLLOW | O_RDWR);
    if (fd == -1) {
        free(out);
        return (struct _buf *)-1;
    }
    out->_fd = (intptr_t) &fd;

    return out;
}

extern "C" int
kobj_get_filesize(struct _buf *file, uint64_t *size)
{
    struct stat st;
    int fd;

    if (!file || !file->_fd || !size) {
        return -1;
    }

    fd = *((int *) file->_fd);
    if (fstat(fd, &st) == -1) {
        return -1;
    }
    *size = st.st_size;
    return 0;
}

extern "C" int
kobj_read_file(struct _buf *file, char *buf, unsigned size, unsigned off)
{
    int fd;

    if (!file || !file->_fd || !buf) {
        return -1;
    }

    fd = *((int *) file->_fd);
    return pread(fd, buf, size, off);
}

extern "C" void
kobj_close_file(struct _buf *file)
{
    int fd;

    if (!file || !file->_fd) {
        return;
    }

    fd = *((int *) file->_fd);
    close(fd);
    free(file);
}
