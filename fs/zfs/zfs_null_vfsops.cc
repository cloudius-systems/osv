/*
 * Copyright (C) 2021 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mount.h>
#include <osv/export.h>

#define zfs_mount   ((vfsop_mount_t)vfs_nullop)
#define zfs_umount  ((vfsop_umount_t)vfs_nullop)
#define zfs_sync    ((vfsop_sync_t)vfs_nullop)
#define zfs_vget    ((vfsop_vget_t)vfs_nullop)
#define zfs_statfs  ((vfsop_statfs_t)vfs_nullop)

static int zfs_noop_mount(struct mount *mp, const char *dev, int flags,
                          const void *data)
{
    printf("The zfs is in-active!. Please add libsolaris.so to the image.\n");
    return -1;
}

/*
 * File system operations
 *
 * This provides dummy vfsops when libsolaris is not loaded and ZFS filesystem
 * is not active.
 */
struct vfsops zfs_vfsops = {
    zfs_noop_mount, /* mount */
    zfs_umount,     /* umount */
    zfs_sync,       /* sync */
    zfs_vget,       /* vget */
    zfs_statfs,     /* statfs */
    nullptr,        /* vnops */
};

extern "C" {
OSV_LIBSOLARIS_API bool zfs_driver_initialized = false;
int zfs_init(void)
{
    return 0;
}
}

//Normally (without ZFS enabled) the zfs_vfsops points to dummy
//noop functions. So when libsolaris.so is loaded, we provide the
//function below to be called to register real vfsops for ZFS
extern "C" OSV_LIBSOLARIS_API void zfs_update_vfsops(struct vfsops* _vfsops) {
    zfs_vfsops.vfs_mount = _vfsops->vfs_mount;
    zfs_vfsops.vfs_unmount = _vfsops->vfs_unmount;
    zfs_vfsops.vfs_sync = _vfsops->vfs_sync;
    zfs_vfsops.vfs_mount = _vfsops->vfs_mount;
    zfs_vfsops.vfs_vget = _vfsops->vfs_vget;
    zfs_vfsops.vfs_statfs = _vfsops->vfs_statfs;
    zfs_vfsops.vfs_vnops = _vfsops->vfs_vnops;
}
