/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_FS_9P_9P_HH
#define OSV_FS_9P_9P_HH

#include <chrono>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <unordered_map>

#include <osv/mutex.h>

#include "session.hh"

struct mount;

// The 9P2000.L filesystem over the session: mount options, per-mount state
// and the vnode glue. Mount state owns the session and the qid map; each
// vnode's v_data owns the unopened fid walked to it, clunked by
// vop_inactive (the root vnode owns the attach fid).
namespace p9 {

// mount(2) data: "trans=tcp,addr=A.B.C.D,port=N,aname=PATH,uid=N" plus
// optional "gid=N,msize=N,timeout=SECONDS,cache=none". Every key at most
// once, no unknown keys, no empty values. msize and timeout may only lower
// the approved maxima (65536 bytes, 30 s). gid defaults to uid.
struct options {
    std::string addr;
    uint16_t port = 0;
    std::string aname;
    uint32_t uid = 0;
    uint32_t gid = 0;
    uint32_t msize = 65536;
    std::chrono::seconds timeout = op_timeout;
};
// 0, or EINVAL for anything not exactly as above.
int parse_options(const char* data, options& o);

// qid.path -> inode number for the mount's lifetime, so a hard link has
// one inode and v_ino never changes after vget(). The first path mapped
// (the root's) gets inode 0, which sys_mount() gives the root vnode.
class qid_map {
public:
    static constexpr size_t cap = 65536;
    // 0 and the inode number; ENFILE once cap paths are mapped.
    int ino(uint64_t path, uint64_t& out);
    size_t size();
private:
    mutex _mtx;
    std::unordered_map<uint64_t, uint64_t> _map;
};

// Rgetattr fields the VFS uses. valid holds at least getattr_needed.
struct attr {
    uint64_t valid;
    uint8_t qtype;
    uint64_t qpath;
    uint32_t mode, uid, gid;
    uint64_t nlink, rdev, size, blocks;
    timespec atime, mtime, ctime;
};
constexpr uint64_t getattr_basic = 0x7ff; // P9_GETATTR_BASIC
// Decode an Rgetattr body: 0, or EIO if malformed or missing a field.
int decode_attr(const bytes& body, attr& a);

struct mount_state {
    std::unique_ptr<session> s;
    qid_map qids;
    std::chrono::seconds timeout;
    uint32_t gid; // create gid, used once creation lands
    uint64_t fsid;
    deadline d() const { return osv::clock::uptime::now() + timeout; }
};

struct inode_state {
    uint32_t fid;
    uint64_t qpath;
};

// Tgetattr(fid, basic) and decode: 0, or the RPC or decode errno.
int getattr(mount_state& ms, uint32_t fid, attr& a);

// vfsops entry points.
int mount(struct mount* mp, const char* dev, int flags, const void* data);
int unmount(struct mount* mp, int flags);

}

#endif
