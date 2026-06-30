/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <unistd.h>

#include <fs/vfs/vfs.h>

void sync()
{
    sys_sync();
}

// syncfs(2) syncs only the filesystem containing the given fd.  OSv has a
// single global buffer cache and no per-mount writeback, so fall back to a
// full sync().  The fd is validated only loosely -- a bad fd still triggers a
// global flush, which is harmless.
void syncfs(int fd)
{
    sys_sync();
}
