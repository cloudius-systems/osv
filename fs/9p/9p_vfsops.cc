/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// 9P2000.L mount: options, session setup and teardown, root vnode.
// Message layouts: diod protocol.md (commit 481911e8), Plan 9 intro(5).

#include "9p.hh"
#include "tcp.hh"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <osv/dentry.h>
#include <osv/mount.h>
#include <osv/vnode.h>

#include "fs/vfs/vfs.h"
#include "fs/vfs/vfs_id.h"

namespace p9 {

namespace {

// Decimal digits only, no sign, at most max.
bool number(const std::string& v, uint64_t max, uint64_t& out)
{
    if (v.empty() || v.size() > 20) {
        return false;
    }
    uint64_t n = 0;
    for (char c : v) {
        if (c < '0' || c > '9' || n > (max - (c - '0')) / 10) {
            return false;
        }
        n = n * 10 + (c - '0');
    }
    out = n;
    return true;
}

uint64_t le(const uint8_t* p, int n)
{
    uint64_t v = 0;
    for (int i = n - 1; i >= 0; i--) {
        v = v << 8 | p[i];
    }
    return v;
}

void put_le(bytes& b, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) {
        b.push_back(uint8_t(v >> 8 * i));
    }
}

std::atomic<uint32_t> mounts{0};

}

int parse_options(const char* data, options& o)
{
    if (!data) {
        return EINVAL;
    }
    enum { trans, addr, port, aname, uid, gid, msize, timeout, cache, nkeys };
    static const char* const keys[nkeys] = {"trans", "addr", "port", "aname",
        "uid", "gid", "msize", "timeout", "cache"};
    bool seen[nkeys] = {};
    options r;
    const std::string s(data);
    size_t at = 0;
    for (;;) {
        size_t end = s.find(',', at);
        std::string kv = s.substr(at, end == s.npos ? s.npos : end - at);
        size_t eq = kv.find('=');
        if (eq == kv.npos || eq + 1 == kv.size()) {
            return EINVAL;
        }
        std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        int i = 0;
        while (i < nkeys && k != keys[i]) {
            i++;
        }
        if (i == nkeys || seen[i]) {
            return EINVAL;
        }
        seen[i] = true;
        uint64_t n = 0;
        in_addr ia;
        switch (i) {
        case trans:
            if (v != "tcp") {
                return EINVAL;
            }
            break;
        case addr:
            if (inet_pton(AF_INET, v.c_str(), &ia) != 1) {
                return EINVAL;
            }
            r.addr = v;
            break;
        case port:
            if (!number(v, 65535, n) || n == 0) {
                return EINVAL;
            }
            r.port = uint16_t(n);
            break;
        case aname:
            r.aname = v;
            break;
        case uid:
        case gid:
            // NONUNAME (~0) is not an identity.
            if (!number(v, 0xfffffffe, n)) {
                return EINVAL;
            }
            (i == uid ? r.uid : r.gid) = uint32_t(n);
            break;
        case msize:
            if (!number(v, 65536, n) || n < min_msize) {
                return EINVAL;
            }
            r.msize = uint32_t(n);
            break;
        case timeout:
            if (!number(v, op_timeout.count(), n) || n == 0) {
                return EINVAL;
            }
            r.timeout = std::chrono::seconds(n);
            break;
        case cache:
            if (v != "none") {
                return EINVAL;
            }
            break;
        }
        if (end == s.npos) {
            break;
        }
        at = end + 1;
    }
    if (!seen[trans] || !seen[addr] || !seen[port] || !seen[aname]
        || !seen[uid] || !seen[gid]) {
        return EINVAL;
    }
    o = r;
    return 0;
}

int qid_map::ino(uint64_t path, uint64_t& out)
{
    WITH_LOCK(_mtx) {
        auto it = _map.find(path);
        if (it != _map.end()) {
            out = it->second;
            return 0;
        }
        if (_map.size() >= cap) {
            return ENFILE;
        }
        uint64_t n = _map.size();
        try {
            _map.emplace(path, n);
        } catch (const std::bad_alloc&) {
            return ENOMEM;
        }
        out = n;
        return 0;
    }
}

size_t qid_map::size()
{
    WITH_LOCK(_mtx) {
        return _map.size();
    }
}

int decode_attr(const bytes& b, attr& a)
{
    // valid[8] qid[13] mode[4] uid[4] gid[4] nlink[8] rdev[8] size[8]
    // blksize[8] blocks[8] atime mtime ctime btime (sec[8] nsec[8]) gen[8]
    // data_version[8]; check_reply() fixed the length.
    if (b.size() != 153) {
        return EIO;
    }
    const uint8_t* p = b.data();
    a.valid = le(p, 8);
    a.qtype = p[8];
    a.qpath = le(p + 13, 8);
    a.mode = uint32_t(le(p + 21, 4));
    a.uid = uint32_t(le(p + 25, 4));
    a.gid = uint32_t(le(p + 29, 4));
    a.nlink = le(p + 33, 8);
    a.rdev = le(p + 41, 8);
    a.size = le(p + 49, 8);
    a.blocks = le(p + 65, 8);
    timespec* t[] = {&a.atime, &a.mtime, &a.ctime};
    for (int i = 0; i < 3; i++) {
        t[i]->tv_sec = time_t(le(p + 73 + 16 * i, 8));
        t[i]->tv_nsec = long(le(p + 81 + 16 * i, 8));
    }
    // The qid type's directory bit (0x80) must agree with the mode.
    bool dir = (a.mode & S_IFMT) == S_IFDIR;
    if ((a.valid & getattr_basic) != getattr_basic
        || dir != bool(a.qtype & 0x80) || a.size > uint64_t(INT64_MAX)) {
        return EIO;
    }
    return 0;
}

int getattr(mount_state& ms, uint32_t fid, attr& a)
{
    bytes body;
    put_le(body, fid, 4);
    put_le(body, getattr_basic, 8);
    frame r;
    int rc = ms.s->rpc(Tgetattr, body, r, ms.d());
    return rc ? rc : decode_attr(r.body, a);
}

int mount(struct mount* mp, const char* dev, int flags, const void* data)
{
    options o;
    if (int rc = parse_options(static_cast<const char*>(data), o)) {
        return rc;
    }
    std::unique_ptr<mount_state> ms(new (std::nothrow) mount_state);
    if (!ms) {
        return ENOMEM;
    }
    ms->timeout = o.timeout;
    ms->gid = o.gid;
    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(o.port);
    inet_pton(AF_INET, o.addr.c_str(), &sin.sin_addr);
    std::unique_ptr<transport> t;
    int rc = connect_tcp(reinterpret_cast<const sockaddr*>(&sin), sizeof sin,
                         ms->d(), t);
    if (rc) {
        return rc;
    }
    ms->s.reset(new (std::nothrow) session(std::move(t)));
    if (!ms->s) {
        return ENOMEM;
    }
    // From here every failure destroys the session (shutdown, join).
    if ((rc = ms->s->negotiate(o.msize, ms->d()))) {
        return rc;
    }
    // No Tauth: afid NOFID, one attach, never a retry as someone else.
    uint32_t fid;
    bytes qid;
    if ((rc = ms->s->attach("", o.aname, o.uid, fid, qid, ms->d()))) {
        return rc;
    }
    attr a;
    uint64_t ino = 0;
    rc = getattr(*ms, fid, a);
    if (!rc && (a.mode & S_IFMT) != S_IFDIR) {
        rc = ENOTDIR;
    }
    if (!rc) {
        rc = ms->qids.ino(a.qpath, ino); // the first mapping: inode 0
    }
    inode_state* root = nullptr;
    if (!rc) {
        root = new (std::nothrow) inode_state{fid, a.qpath};
        rc = root ? 0 : ENOMEM;
    }
    if (rc) {
        ms->s->clunk(fid, ms->d());
        return rc;
    }
    uint32_t n = ++mounts;
    ms->fsid = P9_ID | n;
    mp->m_fsid.__val[0] = int(n);
    mp->m_fsid.__val[1] = int(P9_ID >> 32);
    vnode* vp = mp->m_root->d_vnode;
    vp->v_data = root;
    vp->v_mode = a.mode;
    vp->v_size = off_t(a.size);
    mp->m_flags |= MNT_NONAMECACHE;
    mp->m_data = ms.release();
    return 0;
}

int unmount(struct mount* mp, int flags)
{
    // A vnode besides the root (m_count counts live vnodes) would use the
    // session after it is gone, so even a forced unmount waits for it.
    if (mp->m_count > 1) {
        return EBUSY;
    }
    auto* ms = static_cast<mount_state*>(mp->m_data);
    // ponytail: the root clunk and the session shutdown are RPCs under
    // mount_lock (sys_umount2 holds it); bounded by the mount timeout and
    // flush bounds. Move them out once unmount can drop the lock.
    release_mp_dentries(mp); // the root vnode's inactive clunks its fid
    ms->s->shutdown(ms->d());
    delete ms;
    mp->m_data = nullptr;
    return 0;
}

}

static int p9_sync(struct mount*)
{
    return 0; // nothing is written through this mount yet
}

static int p9_statfs(struct mount*, struct statfs*)
{
    return EOPNOTSUPP;
}

extern struct vnops p9_vnops;

int p9_init(void)
{
    return 0;
}

struct vfsops p9_vfsops = {
    p9::mount,
    p9::unmount,
    p9_sync,
    (vfsop_vget_t)vfs_nullop,
    p9_statfs,
    &p9_vnops,
};
