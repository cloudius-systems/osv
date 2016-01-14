/*
 * Copyright (C) 2015 Scylla, Ltd.
 *
 * Based on ramfs code Copyright (c) 2006-2007, Kohsuke Ohtani
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <cassert>
#include <cerrno>

#include <osv/debug.hh>
#include <osv/vnode.h>
#include <osv/mount.h>
#include <osv/dentry.h>

#include <string>
#include <memory>

#include "nfs.hh"

extern struct vnops nfs_vnops;

/*
 * Mount a file system.
 */
static int nfs_op_mount(struct mount *mp, const char *dev, int flags,
                        const void *data)
{
    assert(mp);

    // build a temporary mount context to check the NFS server is alive
    int err_no;
    std::unique_ptr<mount_context> ctx(new mount_context(mp->m_special));

    if (!ctx->is_valid(err_no)) {
        return err_no;
    }

    struct nfsdir *handle = nullptr;
    int ret = nfs_opendir(ctx->nfs(), "/", &handle);
    if (ret) {
        return -ret;
    }

    mp->m_root->d_vnode->v_data = handle;

    return 0;
}

/*
 * Unmount a file system.
 *
 * Note: There is no nfs_unmount in nfslib.
 *
 */
static int nfs_op_unmount(struct mount *mp, int flags)
{
    assert(mp);

    // Make sure nothing is used under this mount point.
    if (mp->m_count > 1) {
        return EBUSY;
    }

    auto vnode = mp->m_root->d_vnode;

    int err_no;
    auto nfs = get_nfs_context(vnode, err_no);

    if (err_no) {
        return err_no;
    }

    auto handle = get_dir_handle(vnode);
    nfs_closedir(nfs, handle);
    vnode->v_data = nullptr;

    return 0;
}

int nfs_init(void)
{
    return 0;
}

// For the following let's rely on operations on individual files
#define nfs_op_sync    ((vfsop_sync_t)vfs_nullop)
#define nfs_op_vget    ((vfsop_vget_t)vfs_nullop)
#define nfs_op_statfs  ((vfsop_statfs_t)vfs_nullop)

/*
 * File system operations
 */
struct vfsops nfs_vfsops = {
    nfs_op_mount,      /* mount */
    nfs_op_unmount,    /* unmount */
    nfs_op_sync,       /* sync */
    nfs_op_vget,       /* vget */
    nfs_op_statfs,     /* statfs */
    &nfs_vnops,        /* vnops */
};

