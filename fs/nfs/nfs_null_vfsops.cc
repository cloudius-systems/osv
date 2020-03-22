/*
 * Copyright (C) 2015 Scylla, Ltd.
 *
 * Based on ramfs code Copyright (c) 2006-2007, Kohsuke Ohtani
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mount.h>

#define nfs_mount   ((vfsop_mount_t)vfs_nullop)
#define nfs_umount  ((vfsop_umount_t)vfs_nullop)
#define nfs_sync    ((vfsop_sync_t)vfs_nullop)
#define nfs_vget    ((vfsop_vget_t)vfs_nullop)
#define nfs_statfs  ((vfsop_statfs_t)vfs_nullop)

static int nfs_noop_mount(struct mount *mp, const char *dev, int flags,
                          const void *data)
{
    printf("The nfs module is in-active!. Please add nfs module to the image.\n");
    return -1;
}

/*
 * File system operations
 *
 * This desactivate the NFS file system when libnfs is not compiled in.
 *
 */
struct vfsops nfs_vfsops = {
    nfs_noop_mount,      /* mount */
    nfs_umount,     /* umount */
    nfs_sync,       /* sync */
    nfs_vget,       /* vget */
    nfs_statfs,     /* statfs */
    nullptr,        /* vnops */
};

int nfs_init(void)
{
    return 0;
}
