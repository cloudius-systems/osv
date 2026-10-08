/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// The 9P2000.L filesystem (fs/9p/9p_vfsops.cc, 9p_vnops.cc) through
// mount(2), umount(2) and stat(2).
//
// No argument: option parsing, the qid map and Rgetattr decoding, with no
// server. "diod IP PORT UID ANAME": mount cycles against diod (see
// scripts/tests/test_9p.py), identity refusal and a bad aname. "peer IP
// PORT": the fault peer of test_9p.py, one connection per case: an
// Rlerror to Tattach, an Rversion for another dialect, an Rlerror to the
// root Tgetattr. A failed mount must leave nothing mounted; the peer and
// diod report any fid left unclunked.

#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include <osv/dentry.h>
#include <osv/mount.h>

#include <fs/9p/9p.hh>

extern "C" int namei(const char *path, struct dentry **dpp);

static unsigned checks;

static void require(bool ok, const std::string& name)
{
    ++checks;
    if (!ok) {
        printf("FAIL: %s (check %u)\n", name.c_str(), checks);
        exit(1);
    }
}

static int mount9p(const std::string& dir, const std::string& opts)
{
    return mount("9p", dir.c_str(), "9p", 0, opts.c_str()) ? errno : 0;
}

static dev_t dev_of(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) ? dev_t(-1) : st.st_dev;
}

// Nothing is mounted at dir: it is still the /tmp directory and umount
// finds no mount there.
static bool unmounted(const std::string& dir)
{
    return dev_of(dir) == dev_of("/tmp") && umount(dir.c_str()) == -1
        && errno == EINVAL;
}

static const char* const base = "trans=tcp,addr=10.0.0.1,port=564,aname=/x,"
                                "uid=1000,gid=1000";

static void options()
{
    p9::options o;
    require(p9::parse_options(base, o) == 0 && o.addr == "10.0.0.1"
            && o.port == 564 && o.aname == "/x" && o.uid == 1000
            && o.gid == 1000 && o.msize == 65536
            && o.timeout == p9::op_timeout, "minimal options");
    require(p9::parse_options((std::string(base) + ",msize=8192,timeout=5,"
                               "cache=none").c_str(), o) == 0
            && o.msize == 8192 && o.timeout.count() == 5, "optional options");
    const char* bad[] = {
        "",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1000",   // no gid
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,gid=1",      // no uid
        "trans=tcp,addr=10.0.0.1,port=564,uid=1,gid=1",         // no aname
        "trans=tcp,addr=10.0.0.1,aname=/x,uid=1,gid=1",         // no port
        "trans=tcp,port=564,aname=/x,uid=1,gid=1",              // no addr
        "addr=10.0.0.1,port=564,aname=/x,uid=1,gid=1",          // no trans
        "trans=virtio,addr=10.0.0.1,port=564,aname=/x,uid=1,gid=1",
        "trans=tcp,addr=host,port=564,aname=/x,uid=1,gid=1",
        "trans=tcp,addr=10.0.0.1,port=0,aname=/x,uid=1,gid=1",
        "trans=tcp,addr=10.0.0.1,port=65536,aname=/x,uid=1,gid=1",
        "trans=tcp,addr=10.0.0.1,port=-1,aname=/x,uid=1,gid=1",
        "trans=tcp,addr=10.0.0.1,port=564,aname=,uid=1,gid=1",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=4294967295,gid=1",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1x,gid=1",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1,gid=1,uid=2",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1,gid=1,msize=65537",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1,gid=1,msize=4095",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1,gid=1,timeout=31",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1,gid=1,timeout=0",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1,gid=1,cache=mmap",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1,gid=1,ro=1",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1,gid=1,",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1,gid=1,,",
        "trans=tcp,addr=10.0.0.1,port=564,aname=/x,uid=1,gid",
    };
    for (auto b : bad) {
        o = p9::options{};
        o.uid = 77;
        require(p9::parse_options(b, o) == EINVAL && o.uid == 77,
                std::string("rejected: ") + b);
    }
    require(p9::parse_options(nullptr, o) == EINVAL, "no data");
    // Through mount(2): rejected before any connection, nothing mounted.
    const std::string dir = "/tmp/9p-opt";
    mkdir(dir.c_str(), 0755);
    for (auto b : bad) {
        require(mount9p(dir, b) == EINVAL && unmounted(dir),
                std::string("mount(2) rejects: ") + b);
    }
    require(mount("9p", dir.c_str(), "9p", 0, nullptr) == -1
            && errno == EINVAL && unmounted(dir), "mount(2) without data");
    puts("PASS: strict options");
}

static void qids()
{
    std::unique_ptr<p9::qid_map> m(new p9::qid_map);
    uint64_t ino = 99;
    require(m->ino(0xabcdef, ino) == 0 && ino == 0, "first path is inode 0");
    require(m->ino(0xabcdef, ino) == 0 && ino == 0, "same path, same inode");
    for (uint64_t p = 1; p < p9::qid_map::cap; p++) {
        if (m->ino(p << 20, ino) || ino != p) {
            require(false, "path gets the next inode");
        }
    }
    require(m->size() == p9::qid_map::cap, "cap paths mapped");
    require(m->ino(0xabcdef, ino) == 0 && ino == 0, "mapped path at the cap");
    ino = 99;
    require(m->ino(12345, ino) == ENFILE && ino == 99, "path past the cap");
    puts("PASS: qid map");
}

static void put(p9::bytes& b, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) {
        b.push_back(uint8_t(v >> 8 * i));
    }
}

static p9::bytes rgetattr(uint64_t valid, uint8_t qtype, uint32_t mode)
{
    p9::bytes b;
    put(b, valid, 8);
    b.push_back(qtype);
    put(b, 7, 4);
    put(b, 0x1122334455667788ull, 8);
    put(b, mode, 4);
    put(b, 1000, 4);
    put(b, 1001, 4);
    for (uint64_t v : {2ull, 0ull, 4096ull, 4096ull, 8ull, 1ull, 2ull, 3ull,
                       4ull, 5ull, 6ull, 0ull, 0ull, 0ull, 0ull}) {
        put(b, v, 8);
    }
    return b;
}

static void attrs()
{
    p9::attr a;
    require(p9::decode_attr(rgetattr(0x7ff, 0x80, S_IFDIR | 0755), a) == 0
            && a.qpath == 0x1122334455667788ull && a.mode == (S_IFDIR | 0755)
            && a.uid == 1000 && a.gid == 1001 && a.nlink == 2
            && a.size == 4096 && a.blocks == 8 && a.atime.tv_sec == 1
            && a.atime.tv_nsec == 2 && a.ctime.tv_nsec == 6,
            "Rgetattr fields");
    require(p9::decode_attr(rgetattr(0x3fff, 0, S_IFREG | 0644), a) == 0,
            "extra valid bits");
    require(p9::decode_attr(rgetattr(0x7fe, 0x80, S_IFDIR), a) == EIO,
            "missing basic field");
    require(p9::decode_attr(rgetattr(0x7ff, 0, S_IFDIR), a) == EIO,
            "directory mode with a file qid");
    require(p9::decode_attr(rgetattr(0x7ff, 0x80, S_IFREG), a) == EIO,
            "file mode with a directory qid");
    p9::bytes b = rgetattr(0x7ff, 0x80, S_IFDIR);
    b.pop_back();
    require(p9::decode_attr(b, a) == EIO, "short Rgetattr");
    puts("PASS: Rgetattr decoding");
}

static std::string opts(const char* ip, const char* port, const char* aname,
                        unsigned long uid)
{
    return std::string("trans=tcp,addr=") + ip + ",port=" + port + ",aname="
        + aname + ",uid=" + std::to_string(uid) + ",gid="
        + std::to_string(uid) + ",timeout=10";
}

static void diod(const char* ip, const char* port, unsigned long uid,
                 const char* aname)
{
    const std::string a = "/tmp/9p-a", b = "/tmp/9p-b";
    mkdir(a.c_str(), 0755);
    mkdir(b.c_str(), 0755);
    const std::string good = opts(ip, port, aname, uid);
    for (int i = 0; i < 3; i++) {
        int rc = mount9p(a, good);
        printf("diod: mount %d returned %d\n", i, rc);
        require(rc == 0, "mount against diod");
        struct stat st;
        require(stat(a.c_str(), &st) == 0 && S_ISDIR(st.st_mode)
                && st.st_ino == 0 && st.st_dev != dev_of("/tmp"),
                "root is the export directory, inode 0, its own device");
        struct dentry* dp;
        require(namei(a.c_str(), &dp) == 0, "namei the mount point");
        bool nocache = dp->d_mount->m_flags & MNT_NONAMECACHE;
        drele(dp);
        require(nocache, "MNT_NONAMECACHE set");
        require(umount(a.c_str()) == 0 && unmounted(a), "unmount");
    }
    puts("PASS: diod mount and unmount");
    require(mount9p(a, good) == 0 && mount9p(b, good) == 0
            && dev_of(a) != dev_of(b), "two mounts, two devices");
    require(umount(a.c_str()) == 0 && umount(b.c_str()) == 0
            && unmounted(a) && unmounted(b), "unmount both");
    puts("PASS: diod mounts are distinct");
    // diod refuses an identity other than its own uid and a missing
    // export with EPERM; the mount reports it and never retries.
    int rc = mount9p(a, opts(ip, port, aname, uid + 1));
    printf("diod: uid %lu returned %d\n", uid + 1, rc);
    require(rc == EPERM && unmounted(a), "other uid refused");
    rc = mount9p(a, opts(ip, port, "/nonexistent-9p-export", uid));
    printf("diod: missing export returned %d\n", rc);
    require(rc == EPERM && unmounted(a), "missing export refused");
    puts("PASS: diod refusals");
}

// The peer serves each case on its own connection, in this order.
static void peer(const char* ip, const char* port)
{
    const std::string a = "/tmp/9p-a";
    mkdir(a.c_str(), 0755);
    struct fault {
        const char* name;
        const char* aname;
        int want;
    };
    const fault cases[] = {
        {"bad aname", "/bad", ENOENT},
        {"wrong version", "/x", EPROTONOSUPPORT},
        {"root getattr error", "/x", EACCES},
    };
    for (auto& c : cases) {
        int rc = mount9p(a, opts(ip, port, c.aname, 1000));
        printf("peer %s: mount returned %d\n", c.name, rc);
        require(rc == c.want, std::string("errno: ") + c.name);
        require(unmounted(a), std::string("nothing mounted: ") + c.name);
        printf("PASS: peer %s\n", c.name);
    }
}

int main(int argc, char** argv)
{
    mkdir("/tmp", 0755);
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode.empty()) {
        options();
        qids();
        attrs();
    } else if (mode == "diod" && argc == 6) {
        diod(argv[2], argv[3], strtoul(argv[4], nullptr, 10), argv[5]);
    } else if (mode == "peer" && argc == 4) {
        peer(argv[2], argv[3]);
    } else {
        require(false, "known mode and argument count");
    }
    printf("SUMMARY: %u fs checks, 0 failures\n", checks);
    return 0;
}
