/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * Based on ramfs code Copyright (c) 2006-2007, Kohsuke Ohtani
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mount.h>

#define ext_mount   ((vfsop_mount_t)vfs_nullop)
#define ext_umount  ((vfsop_umount_t)vfs_nullop)
#define ext_sync    ((vfsop_sync_t)vfs_nullop)
#define ext_vget    ((vfsop_vget_t)vfs_nullop)
#define ext_statfs  ((vfsop_statfs_t)vfs_nullop)

static int ext_noop_mount(struct mount *mp, const char *dev, int flags,
                          const void *data)
{
    printf("The ext module is in-active!. Please add ext module to the image.\n");
    return -1;
}

/*
 * File system operations
 *
 * This deactivates the EXT file system when libext.so is not loaded.
 *
 */
struct vfsops ext_vfsops = {
    ext_noop_mount, /* mount */
    ext_umount,     /* umount */
    ext_sync,       /* sync */
    ext_vget,       /* vget */
    ext_statfs,     /* statfs */
    nullptr,        /* vnops */
};

extern "C" int ext_init(void)
{
    return 0;
}
