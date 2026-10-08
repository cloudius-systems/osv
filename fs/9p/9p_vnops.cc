/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// 9P2000.L vnode operations. Part A of N5: getattr and inactive only;
// the rest return EOPNOTSUPP until their part lands.

#include "9p.hh"

#include <cerrno>
#include <sys/stat.h>

#include <osv/mount.h>
#include <osv/vnode.h>
#include <osv/vnode_attr.h>

namespace {

p9::mount_state& ms_of(vnode* vp)
{
    return *static_cast<p9::mount_state*>(vp->v_mount->m_data);
}

vtype type_of(uint32_t mode)
{
    switch (mode & S_IFMT) {
    case S_IFDIR: return VDIR;
    case S_IFREG: return VREG;
    case S_IFLNK: return VLNK;
    case S_IFIFO: return VFIFO;
    case S_IFSOCK: return VSOCK;
    case S_IFCHR: return VCHR;
    case S_IFBLK: return VBLK;
    default: return VNON;
    }
}

int p9_getattr(vnode* vp, vattr* va)
{
    auto* in = static_cast<p9::inode_state*>(vp->v_data);
    p9::attr a;
    if (int rc = p9::getattr(ms_of(vp), in->fid, a)) {
        return rc;
    }
    // The server must not change what object a live fid names.
    if (a.qpath != in->qpath || type_of(a.mode) != vtype(vp->v_type)) {
        return EIO;
    }
    va->va_mask = AT_NBLOCKS;
    va->va_type = vtype(vp->v_type);
    va->va_mode = a.mode & 07777;
    va->va_nlink = a.nlink;
    va->va_uid = a.uid;
    va->va_gid = a.gid;
    va->va_fsid = ms_of(vp).fsid;
    va->va_nodeid = vp->v_ino;
    va->va_atime = a.atime;
    va->va_mtime = a.mtime;
    va->va_ctime = a.ctime;
    va->va_rdev = a.rdev;
    va->va_nblocks = a.blocks;
    va->va_size = off_t(a.size);
    vp->v_mode = a.mode;
    vp->v_size = off_t(a.size);
    return 0;
}

int p9_inactive(vnode* vp)
{
    auto* in = static_cast<p9::inode_state*>(vp->v_data);
    if (in) {
        ms_of(vp).s->clunk(in->fid, ms_of(vp).d());
        delete in;
        vp->v_data = nullptr;
    }
    return 0;
}

int p9_unsupported()
{
    return EOPNOTSUPP;
}

}

#define U(t) ((t)p9_unsupported)

struct vnops p9_vnops = {
    U(vnop_open_t),
    U(vnop_close_t),
    U(vnop_read_t),
    U(vnop_write_t),
    U(vnop_seek_t),
    U(vnop_ioctl_t),
    U(vnop_fsync_t),
    U(vnop_readdir_t),
    U(vnop_lookup_t),
    U(vnop_create_t),
    U(vnop_remove_t),
    U(vnop_rename_t),
    U(vnop_mkdir_t),
    U(vnop_rmdir_t),
    p9_getattr,
    U(vnop_setattr_t),
    p9_inactive,
    U(vnop_truncate_t),
    U(vnop_link_t),
    nullptr, /* cache */
    U(vnop_fallocate_t),
    U(vnop_readlink_t),
    U(vnop_symlink_t),
    nullptr, /* create_open */
    nullptr, /* access */
};
