/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <unistd.h>
#include <errno.h>

#include <fs/vfs/vfs.h>
#include <osv/file.h>

void sync()
{
    sys_sync();
}

// syncfs(2) syncs only the filesystem containing the given fd.  OSv has a
// single global buffer cache and no per-mount writeback, so once the fd is
// validated we fall back to a full sync().  Returns 0 on success, or -1 with
// errno set (EBADF) for an invalid descriptor, matching the Linux prototype
// int syncfs(int).
int syncfs(int fd)
{
    struct file *fp;
    int error = fget(fd, &fp);
    if (error) {
        errno = error;
        return -1;
    }
    fdrop(fp);
    sys_sync();
    return 0;
}
