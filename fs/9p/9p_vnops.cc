/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// 9P2000.L vnode operations. Lookup walks one name and asks Tgetattr;
// open clones the vnode's fid and Tlopens it; read and readdir use the open
// fid; close clunks it. Write returns EOPNOTSUPP: VOP_WRITE has no file, so
// it cannot name the open fid it would be authorized by.
// Layouts: diod protocol.md (commit 481911e8).

#include "9p.hh"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <new>
#include <sys/stat.h>

#include <osv/file.h>
#include <osv/fcntl.h>
#include <osv/mount.h>
#include <osv/uio.h>
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

uint64_t le(const uint8_t* p, int n)
{
    uint64_t v = 0;
    for (int i = n - 1; i >= 0; i--) {
        v = v << 8 | p[i];
    }
    return v;
}

void put_le(p9::bytes& b, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) {
        b.push_back(uint8_t(v >> 8 * i));
    }
}

p9::inode_state* in_of(vnode* vp)
{
    return static_cast<p9::inode_state*>(vp->v_data);
}

p9::open_state* os_of(file* fp)
{
    return static_cast<p9::open_state*>(file_data(fp));
}

// One name; a name the VFS should never pass is refused, not walked.
int p9_lookup(vnode* dvp, char* name, vnode** vpp)
{
    *vpp = nullptr;
    if (!*name || strchr(name, '/') || !strcmp(name, ".")
        || !strcmp(name, "..")) {
        return EINVAL;
    }
    if (dvp->v_type != VDIR) {
        return ENOTDIR;
    }
    auto& ms = ms_of(dvp);
    uint32_t fid;
    p9::bytes qid;
    int rc = ms.s->walk(in_of(dvp)->fid, {name}, fid, qid, ms.d());
    if (rc) {
        return rc;
    }
    p9::attr a;
    uint64_t ino = 0;
    rc = p9::getattr(ms, fid, a);
    if (!rc && le(qid.data() + 5, 8) != a.qpath) {
        rc = EIO; // Rwalk and Rgetattr disagree on the object
    }
    if (!rc && type_of(a.mode) == VNON) {
        rc = EIO;
    }
    if (!rc) {
        rc = ms.qids.ino(a.qpath, ino);
    }
    vnode* vp = nullptr;
    if (!rc) {
        // No RPC under the vnode table lock: vget() only allocates.
        bool live = vget(dvp->v_mount, ino, &vp);
        if (!vp) {
            rc = ENOMEM;
        } else if (live && in_of(vp)) {
            // Already live (another name, or held): it keeps its fid.
            if (vtype(vp->v_type) != type_of(a.mode)) {
                vput(vp);
                rc = EIO; // the server reused a live identity
            } else {
                ms.s->clunk(fid, ms.d());
                *vpp = vp;
                return 0;
            }
        } else {
            // New, or left without a fid by a lookup that failed after
            // vget() while we waited for its lock: this fid becomes its.
            auto* in = new (std::nothrow) p9::inode_state{fid, a.qpath};
            if (!in) {
                vput(vp);
                rc = ENOMEM;
            } else {
                vp->v_data = in;
                vp->v_type = type_of(a.mode);
                vp->v_mode = a.mode;
                vp->v_size = off_t(a.size);
                *vpp = vp;
                return 0;
            }
        }
    }
    ms.s->clunk(fid, ms.d());
    return rc;
}

// OSv fflags to Linux open flags for Tlopen (the .L wire values), never a
// raw cast. O_CREAT, O_EXCL, O_NOCTTY and O_NOFOLLOW are the VFS's.
uint32_t lflags(int f)
{
    uint32_t l = (f & FWRITE) ? ((f & FREAD) ? 2 : 1) : 0;
    if (f & O_TRUNC) {
        l |= 01000;
    }
    if (f & O_APPEND) {
        l |= 02000;
    }
    if (f & O_DSYNC) {
        l |= 010000;
    }
    if ((f & O_SYNC) == O_SYNC) {
        l |= 04010000;
    }
    return l;
}

int p9_open(file* fp)
{
    vnode* vp = file_dentry(fp)->d_vnode;
    auto& ms = ms_of(vp);
    if (vp->v_type != VREG && vp->v_type != VDIR) {
        return EOPNOTSUPP; // no special-file I/O over 9P
    }
    // The VFS checked write access. O_TRUNC is sent with Tlopen, but the
    // VFS calls vop_truncate (unsupported) first unless vop_create_open is
    // set, so until then an O_TRUNC open fails EOPNOTSUPP.
    uint32_t fid;
    p9::bytes qid;
    int rc = ms.s->walk(in_of(vp)->fid, {}, fid, qid, ms.d());
    if (rc) {
        return rc;
    }
    p9::bytes body;
    put_le(body, fid, 4);
    put_le(body, lflags(fp->f_flags), 4);
    p9::frame r;
    rc = ms.s->rpc(p9::Tlopen, body, r, ms.d());
    p9::open_state* os = nullptr;
    if (!rc) {
        // Rlopen qid[13] iounit[4]; 0 means msize - io_header.
        if (le(r.body.data() + 5, 8) != in_of(vp)->qpath) {
            rc = EIO;
        }
    }
    if (!rc) {
        uint32_t cap = ms.s->msize() - p9::io_header;
        uint32_t iou = uint32_t(le(r.body.data() + 13, 4));
        os = new (std::nothrow) p9::open_state;
        rc = os ? 0 : ENOMEM;
        if (os) {
            os->fid = fid;
            os->iounit = iou && iou < cap ? iou : cap;
        }
    }
    if (rc) {
        ms.s->clunk(fid, ms.d());
        return rc;
    }
    file_setdata(fp, os);
    return 0;
}

int p9_close(vnode* vp, file* fp)
{
    auto* os = os_of(fp);
    if (!os) {
        return 0;
    }
    auto& ms = ms_of(vp);
    int rc = ms.s->clunk(os->fid, ms.d());
    delete os;
    file_setdata(fp, nullptr);
    return rc;
}

// The offset and count come from the caller; each Tread asks at most
// iounit. EOF is the server's zero count, never a cached size. An error
// after some bytes moved returns those bytes; the next read reports it.
int p9_read(vnode* vp, file* fp, uio* uio, int)
{
    if (vp->v_type == VDIR) {
        return EISDIR;
    }
    if (uio->uio_offset < 0) {
        return EINVAL;
    }
    auto* os = os_of(fp);
    auto& ms = ms_of(vp);
    bool moved = false;
    while (uio->uio_resid > 0) {
        uint32_t n = uint32_t(std::min<ssize_t>(uio->uio_resid, os->iounit));
        p9::bytes body;
        put_le(body, os->fid, 4);
        put_le(body, uint64_t(uio->uio_offset), 8);
        put_le(body, n, 4);
        p9::frame r;
        if (int rc = ms.s->rpc(p9::Tread, body, r, ms.d())) {
            return moved ? 0 : rc; // bytes already moved are returned
        }
        uint32_t got = uint32_t(le(r.body.data(), 4)); // <= n, checked
        if (got == 0) {
            break;
        }
        // A short count is not EOF (9P allows it anywhere); only zero is.
        uiomove(r.body.data() + 4, got, uio);
        moved = true;
    }
    return 0;
}

}

namespace p9 {

int parse_dirent(const uint8_t* p, size_t left, dirent_rec& e)
{
    if (left < 24 || left < 24 + le(p + 22, 2)) {
        return EIO;
    }
    size_t len = le(p + 22, 2);
    if (len == 0 || len > max_name || memchr(p + 24, '/', len)
        || memchr(p + 24, 0, len) || le(p + 13, 8) > uint64_t(INT64_MAX)) {
        return EIO;
    }
    e.qpath = le(p + 5, 8);
    e.cookie = le(p + 13, 8);
    e.type = p[21];
    e.name = reinterpret_cast<const char*>(p + 24);
    e.len = len;
    e.size = 24 + len;
    return 0;
}

}

namespace {

// One entry per call. The server's cookie for the next entry is kept in
// f_offset (so telldir/seekdir carry cookies, not indexes).
int p9_readdir(vnode* vp, file* fp, dirent* dir)
{
    static_assert(sizeof(dir->d_name) > p9::max_name, "d_name too short");
    auto* os = os_of(fp);
    auto& ms = ms_of(vp);
    if (os->dcookie != fp->f_offset || os->dpos >= os->dir.size()) {
        p9::bytes body;
        put_le(body, os->fid, 4);
        put_le(body, uint64_t(fp->f_offset), 8);
        put_le(body, os->iounit, 4);
        p9::frame r;
        if (int rc = ms.s->rpc(p9::Treaddir, body, r, ms.d())) {
            return rc;
        }
        os->dir.assign(r.body.begin() + 4, r.body.end());
        os->dpos = 0;
        os->dcookie = fp->f_offset;
        if (os->dir.empty()) {
            return ENOENT;
        }
    }
    p9::dirent_rec e;
    if (p9::parse_dirent(os->dir.data() + os->dpos,
                         os->dir.size() - os->dpos, e)) {
        os->dir.clear();
        return EIO;
    }
    uint64_t ino;
    bool dotdot = e.len == 2 && !memcmp(e.name, "..", 2);
    if (dotdot && (vp->v_flags & VROOT)) {
        ino = vp->v_ino; // the export's parent is not in the mount
    } else if (int rc = ms.qids.ino(e.qpath, ino)) {
        return rc;
    }
    dir->d_ino = ino;
    dir->d_off = off_t(e.cookie);
    dir->d_type = e.type;
    memcpy(dir->d_name, e.name, e.len);
    dir->d_name[e.len] = '\0';
    os->dpos += e.size;
    fp->f_offset = dir->d_off;
    os->dcookie = fp->f_offset;
    return 0;
}

int p9_seek(vnode*, file*, off_t, off_t)
{
    return 0; // offsets are the caller's; directory offsets are cookies
}

int p9_unsupported()
{
    return EOPNOTSUPP;
}

}

#define U(t) ((t)p9_unsupported)

struct vnops p9_vnops = {
    p9_open,
    p9_close,
    p9_read,
    U(vnop_write_t), /* (w) needs the open fid: P3b vop_write_file */
    p9_seek,
    U(vnop_ioctl_t),
    U(vnop_fsync_t),
    p9_readdir,
    p9_lookup,
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
