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

// f_data of an open file: a fid cloned from the vnode's and Tlopened, so
// I/O uses what this open was authorized for, never a path or another
// open's fid. For a directory, the last Rreaddir is kept with the cookie it
// was read at and the position in it.
struct open_state {
    uint32_t fid;
    uint32_t iounit; // largest Tread count to send
    bytes dir;
    size_t dpos = 0;
    off_t dcookie = -1; // f_offset that dir[dpos] continues from
};

// Linux P9_IOHDRSZ: Tread/Twrite headers fit in it, so a count of
// msize - io_header always fits a frame.
constexpr uint32_t io_header = 24;

// One Rreaddir record at p, with left bytes from p to the end of the
// reply: qid[13] offset[8] type[1] name[s]. 0, or EIO if it runs past
// left, the name is empty, longer than max_name or holds '/' or NUL, or
// the cookie (offset, for the next entry) is above INT64_MAX. Reads
// nothing at or past p + left.
struct dirent_rec {
    uint64_t qpath;
    uint64_t cookie;
    uint8_t type;
    const char* name; // not terminated
    size_t len;
    size_t size; // bytes the record takes
};
constexpr size_t max_name = 255; // d_name holds it and a NUL
int parse_dirent(const uint8_t* p, size_t left, dirent_rec& e);

// Little-endian n-byte (n <= 8) field at p; append v as n bytes.
uint64_t le(const uint8_t* p, int n);
void put_le(bytes& b, uint64_t v, int n);

// Tgetattr(fid, basic) and decode: 0, or the RPC or decode errno.
int getattr(mount_state& ms, uint32_t fid, attr& a);

// vfsops entry points.
int mount(struct mount* mp, const char* dev, int flags, const void* data);
int unmount(struct mount* mp, int flags);

}

#endif
