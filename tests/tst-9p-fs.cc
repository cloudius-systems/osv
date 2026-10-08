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
// scripts/tests/test_9p.py), identity refusal and a bad aname, then the
// files test_9p.py prepared in the export: contents, reads across iounit
// chunks, directory listings across several Rreaddir replies, hard link
// identity, and the (w) group. "peer IP PORT": the fault peer of
// test_9p.py, one connection per case: mount failures, then lookup, open,
// read and readdir faults. A failed mount must leave nothing mounted; the
// peer and diod report any fid left unclunked.

#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

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

static std::string slurp(const std::string& path)
{
    std::string r;
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return "open failed";
    }
    char buf[100];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        r.append(buf, n);
    }
    close(fd);
    return n < 0 ? "read failed" : r;
}

static p9::session& session_of(const std::string& dir)
{
    struct dentry* dp;
    require(namei(dir.c_str(), &dp) == 0, "namei " + dir);
    auto* ms = static_cast<p9::mount_state*>(dp->d_mount->m_data);
    drele(dp);
    return *ms->s;
}

constexpr size_t big_size = 1000000; // byte i of big is i % 251
constexpr unsigned dir_entries = 400; // d/e000 .. d/e399, and d/h2

// Every entry of dir, by name: d_ino, the order read and the cookie after
// it. Also saves telldir() after entry `mark` and the entry after it.
struct listing {
    std::vector<std::string> names;
    std::vector<ino_t> inos;
    long mark_pos = -1;
    std::string after_mark;
    int err = 0;
};

static listing list(const std::string& dir, size_t mark = 0)
{
    listing l;
    DIR* d = opendir(dir.c_str());
    if (!d) {
        l.err = errno;
        return l;
    }
    for (;;) {
        errno = 0;
        dirent* e = readdir(d);
        if (!e) {
            l.err = errno;
            break;
        }
        if (mark && l.names.size() == mark + 1) {
            l.after_mark = e->d_name;
        }
        l.names.push_back(e->d_name);
        l.inos.push_back(e->d_ino);
        if (mark && l.names.size() == mark + 1) {
            l.mark_pos = telldir(d);
        }
    }
    if (l.mark_pos >= 0) {
        seekdir(d, l.mark_pos);
        dirent* e = readdir(d);
        if (!e || l.after_mark != e->d_name) {
            l.err = EILSEQ; // seekdir did not return to the cookie
        }
    }
    closedir(d);
    return l;
}

// Each of e000..e399 and h2 exactly once; "." and ".." at most once each
// (the server decides whether to list them); nothing else.
static bool full_listing(const listing& l)
{
    std::set<std::string> want{"h2"};
    for (unsigned i = 0; i < dir_entries; i++) {
        char n[8];
        snprintf(n, sizeof n, "e%03u", i);
        want.insert(n);
    }
    std::set<std::string> got(l.names.begin(), l.names.end());
    size_t dots = got.erase(".") + got.erase("..");
    return !l.err && got == want && l.names.size() == want.size() + dots;
}

static ino_t ino_of(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) ? ino_t(-1) : st.st_ino;
}

// The files test_9p.py prepared in the export (hello, big, out holding what
// tst-9p-tcp wrote, fifo, h1, d/), read through the mount.
static void files(const char* ip, const char* port, unsigned long uid,
                  const char* aname)
{
    const std::string a = "/tmp/9p-a";
    mkdir(a.c_str(), 0755);
    require(mount9p(a, opts(ip, port, aname, uid)) == 0, "mount for files");
    require(slurp(a + "/hello") == "hello from the 9p export\n",
            "hello contents");
    require(slurp(a + "/h1") == "one object, two names\n", "h1 contents");
    struct stat st;
    require(stat((a + "/hello").c_str(), &st) == 0 && S_ISREG(st.st_mode)
            && st.st_size == 25 && st.st_ino != 0, "stat hello");
    require(stat((a + "/nothere").c_str(), &st) == -1 && errno == ENOENT,
            "missing name is ENOENT");
    puts("PASS: diod read files");

    // 200000 bytes at msize 65536 take four Treads (iounit 65512); the
    // read returns all of them, not just the first chunk.
    int fd = open((a + "/big").c_str(), O_RDONLY);
    require(fd >= 0, "open big");
    std::vector<char> buf(300000);
    size_t at = 0;
    bool same = true;
    for (;;) {
        ssize_t n = read(fd, buf.data(), 200000);
        require(n >= 0, "read big");
        if (n == 0) {
            break;
        }
        require(size_t(n) == std::min<size_t>(200000, big_size - at),
                "a read spanning chunks returns every byte asked");
        for (ssize_t i = 0; i < n; i++) {
            same = same && buf[i] == char((at + i) % 251);
        }
        at += n;
    }
    require(same && at == big_size, "big contents");
    require(pread(fd, buf.data(), 1000, big_size - 500) == 500
            && buf[0] == char((big_size - 500) % 251), "pread before EOF");
    require(pread(fd, buf.data(), 1000, big_size) == 0, "pread at EOF");
    require(read(fd, buf.data(), 10) == 0, "read at EOF");
    close(fd);
    fd = open(a.c_str(), O_RDONLY);
    require(fd >= 0 && read(fd, buf.data(), 10) == -1 && errno == EISDIR,
            "read of a directory is EISDIR");
    close(fd);
    require(open((a + "/fifo").c_str(), O_RDONLY | O_NONBLOCK) == -1
            && errno == EOPNOTSUPP, "a FIFO is not opened over 9P");
    puts("PASS: diod read across iounit chunks");

    // Hard link: one qid, one inode, under both names and in readdir.
    ino_t i1 = ino_of(a + "/h1");
    require(i1 != ino_t(-1) && i1 == ino_of(a + "/d/h2")
            && i1 != ino_of(a + "/hello"), "h1 and d/h2 share st_ino");
    require(stat((a + "/h1").c_str(), &st) == 0 && st.st_nlink == 2,
            "h1 has two links");
    fd = open((a + "/h1").c_str(), O_RDONLY);
    require(fd >= 0, "open h1");
    // With h1's vnode held, lookups of both names reuse it.
    for (int i = 0; i < 3; i++) {
        require(ino_of(a + "/d/h2") == i1 && ino_of(a + "/h1") == i1,
                "lookups of a held object");
    }
    require(slurp(a + "/d/h2") == "one object, two names\n",
            "d/h2 contents while h1 is open");
    close(fd);
    listing l = list(a + "/d");
    require(full_listing(l), "readdir lists every entry");
    for (size_t i = 0; i < l.names.size(); i++) {
        if (l.names[i] == "h2") {
            require(l.inos[i] == i1, "readdir d_ino of h2 is h1's st_ino");
        }
    }
    listing r = list(a);
    require(!r.err && std::set<std::string>(r.names.begin(), r.names.end())
            .count("hello"), "readdir of the root");
    for (size_t i = 0; i < r.names.size(); i++) {
        if (r.names[i] == "." || r.names[i] == "..") {
            require(r.inos[i] == 0, "the root's " + r.names[i]
                    + " is the root inode");
        }
    }
    puts("PASS: diod hardlink identity");

    // (w): VOP_WRITE has no file, so it cannot use the open fid; until a
    // per-open write hook exists, writes are refused and the host file
    // keeps its bytes (test_9p.py checks out after this run).
    fd = open((a + "/out").c_str(), O_WRONLY);
    require(fd >= 0, "(w) open out for writing");
    require(write(fd, "x", 1) == -1 && errno == EOPNOTSUPP,
            "(w) write is EOPNOTSUPP");
    require(close(fd) == 0, "(w) close");
    puts("PASS: (w) write unsupported");

    // Everything is closed: only the root's fid is left.
    unsigned live = session_of(a).fids();
    printf("diod: %u fids live after close\n", live);
    require(live == 1, "only the root fid is live");
    require(umount(a.c_str()) == 0 && unmounted(a), "unmount after files");

    // msize 4096: d needs several Rreaddir replies (about 145 entries
    // each), and telldir/seekdir carry the server's cookies.
    require(mount9p(a, opts(ip, port, aname, uid) + ",msize=4096") == 0,
            "mount msize 4096");
    l = list(a + "/d", 200);
    printf("diod: msize 4096 listing: %zu entries, err %d\n",
           l.names.size(), l.err);
    require(full_listing(l), "readdir across Rreaddir replies");
    require(l.mark_pos >= 0, "telldir after entry 200");
    require(slurp(a + "/hello") == "hello from the 9p export\n",
            "hello at msize 4096");
    fd = open((a + "/big").c_str(), O_RDONLY);
    require(fd >= 0 && read(fd, buf.data(), 300000) == 300000
            && buf[299999] == char(299999 % 251), "big at msize 4096");
    close(fd);
    require(session_of(a).fids() == 1, "only the root fid, msize 4096");
    require(umount(a.c_str()) == 0 && unmounted(a), "unmount msize 4096");
    puts("PASS: diod readdir across Rreaddir replies");
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
    // I/O faults, one mount each. The peer serves f (1000 bytes, byte i is
    // i % 251), p (a FIFO), u (no file type) and g in its root.
    const std::string f = a + "/f";
    struct stat st;
    char buf[2000];
    auto io = [&](const char* name, std::function<int()> op, int want) {
        require(mount9p(a, opts(ip, port, "/x", 1000)) == 0,
                std::string("mount: ") + name);
        errno = 0;
        int rc = op();
        printf("peer %s: returned %d\n", name, rc);
        require(rc == want, std::string("errno: ") + name);
        require(umount(a.c_str()) == 0 && unmounted(a),
                std::string("unmount: ") + name);
        printf("PASS: peer %s\n", name);
    };
    auto err = [](int rc) { return rc < 0 ? errno : 0; };
    auto readall = [&](int want_n, int want_after) {
        int fd = open(f.c_str(), O_RDONLY);
        if (fd < 0) {
            return errno;
        }
        ssize_t n = read(fd, buf, 1000);
        bool ok = n == want_n;
        for (int i = 0; ok && i < n; i++) {
            ok = buf[i] == char(i % 251);
        }
        int after = err(read(fd, buf, 1000));
        close(fd);
        return ok && after == want_after ? 0 : -1;
    };
    auto readdir_err = [&]() {
        DIR* d = opendir(a.c_str());
        if (!d) {
            return -2;
        }
        errno = 0;
        int rc = readdir(d) ? 0 : errno;
        closedir(d);
        return rc;
    };
    io("lookup getattr error", [&] { return err(stat(f.c_str(), &st)); },
       EACCES);
    io("lookup qid mismatch", [&] { return err(stat(f.c_str(), &st)); }, EIO);
    io("lookup unknown type", [&] {
        return err(stat((a + "/u").c_str(), &st));
    }, EIO);
    // g claims f's qid path as a directory while f's vnode is held.
    io("lookup reused identity", [&] {
        int fd = open(f.c_str(), O_RDONLY);
        if (fd < 0) {
            return -1;
        }
        int rc = err(stat((a + "/g").c_str(), &st));
        close(fd);
        return rc;
    }, EIO);
    io("lopen error", [&] { return err(open(f.c_str(), O_RDONLY)); }, EACCES);
    io("lopen qid mismatch", [&] { return err(open(f.c_str(), O_RDONLY)); },
       EIO);
    io("special file", [&] {
        if (stat((a + "/p").c_str(), &st) || !S_ISFIFO(st.st_mode)) {
            return -1;
        }
        return err(open((a + "/p").c_str(), O_RDONLY | O_NONBLOCK));
    }, EOPNOTSUPP);
    // Rlopen iounit 100: Treads of at most 100 (the first answered with
    // 60 bytes, which is not EOF), then EOF.
    io("iounit", [&] { return readall(1000, 0); }, 0);
    // An Rlerror after the first chunk: the read returns the 100 bytes it
    // has; the next read reports the error.
    io("read error after progress", [&] { return readall(100, EIO); }, 0);
    io("readdir short record", readdir_err, EIO);
    io("readdir slash in name", readdir_err, EIO);
    io("readdir cookie past off_t", readdir_err, EIO);
    const std::string n = a + "/n";
    io("lcreate error", [&] {
        return err(open(n.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600));
    }, EACCES);
    // Rlcreate names one object, a walk to the name another: replaced.
    io("lcreate replaced", [&] {
        return err(open(n.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600));
    }, EIO);
    io("mkdir error", [&] { return err(mkdir(n.c_str(), 0700)); }, EACCES);
    io("unlink error", [&] { return err(unlink(f.c_str())); }, EACCES);
    io("rename error", [&] {
        return err(rename(f.c_str(), (a + "/f2").c_str()));
    }, EXDEV);
    // Trenameat unsupported (EOPNOTSUPP), then Trename of f's fid: EXDEV.
    io("rename fallback error", [&] {
        return err(rename(f.c_str(), (a + "/f2").c_str()));
    }, EXDEV);
    // Tunlinkat unsupported, then Tremove of a fid walked to f: EACCES,
    // and the fid is released all the same.
    io("unlink fallback error", [&] { return err(unlink(f.c_str())); },
       EACCES);
    io("setattr error", [&] { return err(chmod(f.c_str(), 0600)); }, EPERM);
    io("statfs error", [&] {
        struct statfs sf;
        return err(statfs(a.c_str(), &sf));
    }, ENOSYS);
    // diod takes no Tunlinkat, so only the peer sees rmdir's flag: it
    // requires AT_REMOVEDIR and answers EACCES.
    io("rmdir flag", [&] { return err(rmdir((a + "/e").c_str())); }, EACCES);
}

// Changes through the mount: create (Tlcreate hands its open fid to the
// file), O_TRUNC, mkdir, rename, rmdir, unlink, chmod, statfs, and a file
// the host replaces while the guest holds it open (test_9p.py replaces
// victim once ready exists, then creates replaced).
static void changes(const char* ip, const char* port, unsigned long uid,
                    const char* aname)
{
    const std::string a = "/tmp/9p-a";
    require(mount9p(a, opts(ip, port, aname, uid)) == 0, "mount for changes");
    struct stat st, st2;
    char buf[100];
    int fd = open((a + "/kept").c_str(), O_CREAT | O_EXCL | O_RDWR, 0640);
    require(fd >= 0, "create kept");
    require(fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 0
            && (st.st_mode & 07777) == 0640, "fstat of the created file");
    require(stat((a + "/kept").c_str(), &st2) == 0 && st2.st_ino == st.st_ino,
            "the name is the created object");
    require(read(fd, buf, 1) == 0, "a new file reads EOF");
    require(write(fd, "x", 1) == -1 && errno == EOPNOTSUPP,
            "(w) write to a created file is EOPNOTSUPP");
    require(close(fd) == 0, "close kept");
    require(open((a + "/kept").c_str(), O_CREAT | O_EXCL | O_RDWR, 0640) == -1
            && errno == EEXIST, "O_EXCL on an existing name");
    fd = open((a + "/hello").c_str(), O_CREAT | O_RDONLY, 0600);
    require(fd >= 0 && read(fd, buf, sizeof buf) == 25,
            "O_CREAT opens an existing file");
    close(fd);
    fd = open((a + "/trunc").c_str(), O_WRONLY | O_TRUNC);
    require(fd >= 0 && fstat(fd, &st) == 0 && st.st_size == 0,
            "O_TRUNC truncates on the server");
    close(fd);
    puts("PASS: diod create");

    require(mkdir((a + "/nd").c_str(), 0750) == 0, "mkdir");
    require(stat((a + "/nd").c_str(), &st) == 0 && S_ISDIR(st.st_mode),
            "stat the new directory");
    require(mkdir((a + "/nd").c_str(), 0750) == -1 && errno == EEXIST,
            "mkdir of an existing name");
    fd = open((a + "/nd/t").c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    require(fd >= 0 && close(fd) == 0, "create in the new directory");
    ino_t t = ino_of(a + "/nd/t");
    require(rename((a + "/nd/t").c_str(), (a + "/moved").c_str()) == 0,
            "rename across directories");
    require(ino_of(a + "/moved") == t && stat((a + "/nd/t").c_str(), &st) == -1
            && errno == ENOENT, "the object moved");
    require(rmdir((a + "/d").c_str()) == -1 && errno == ENOTEMPTY,
            "rmdir of a full directory");
    require(rmdir((a + "/nd").c_str()) == 0 && stat((a + "/nd").c_str(), &st)
            == -1 && errno == ENOENT, "rmdir");
    require(unlink((a + "/moved").c_str()) == 0
            && stat((a + "/moved").c_str(), &st) == -1 && errno == ENOENT,
            "unlink");
    require(unlink((a + "/moved").c_str()) == -1 && errno == ENOENT,
            "unlink of a missing name");
    puts("PASS: diod namespace changes");

    const std::string m = a + "/modef";
    require(chmod(m.c_str(), 0600) == 0 && stat(m.c_str(), &st) == 0
            && (st.st_mode & 07777) == 0600, "chmod");
    require(utimes(m.c_str(), nullptr) == -1 && errno == EOPNOTSUPP,
            "utimes is refused, not ignored");
    require(stat(m.c_str(), &st) == 0 && (st.st_mode & 07777) == 0600,
            "the refused utimes left the mode");
    puts("PASS: diod chmod");

    struct statfs sf;
    require(statfs(a.c_str(), &sf) == 0, "statfs");
    snprintf(buf, sizeof buf, "%lu %llu %lu\n", (unsigned long)sf.f_bsize,
             (unsigned long long)sf.f_blocks, (unsigned long)sf.f_namelen);
    std::string host = slurp(a + "/statfs.txt");
    printf("diod: statfs %s host %s", buf, host.c_str());
    require(host == buf && sf.f_type == 0x01021997 && sf.f_bfree <= sf.f_blocks,
            "statfs is the server's");
    puts("PASS: diod statfs");

    fd = open((a + "/victim").c_str(), O_RDONLY);
    require(fd >= 0, "open victim");
    ino_t old = ino_of(a + "/victim");
    int r = open((a + "/ready").c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    require(r >= 0 && close(r) == 0, "tell the host to replace victim");
    bool replaced = false;
    for (int i = 0; i < 300 && !replaced; i++) {
        replaced = stat((a + "/replaced").c_str(), &st) == 0;
        if (!replaced) {
            usleep(100000);
        }
    }
    require(replaced, "the host replaced victim");
    require(pread(fd, buf, sizeof buf, 0) == 11
            && !memcmp(buf, "old object\n", 11),
            "the open fd still reads the old object");
    require(slurp(a + "/victim") == "new object\n",
            "the name opens the new object");
    require(ino_of(a + "/victim") != old, "the name is a new inode");
    close(fd);
    puts("PASS: diod replacement while open");

    unsigned live = session_of(a).fids();
    printf("diod: %u fids live after changes\n", live);
    require(live == 1, "only the root fid after changes");
    require(umount(a.c_str()) == 0 && unmounted(a), "unmount after changes");
}

// Each record is placed to end on a page boundary followed by a
// PROT_NONE page, so a parser that reads past `left` faults.
static void dirents()
{
    long pg = sysconf(_SC_PAGESIZE);
    auto* m = static_cast<uint8_t*>(mmap(nullptr, 2 * pg,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    require(m != MAP_FAILED && mprotect(m + pg, pg, PROT_NONE) == 0,
            "guard page");
    auto rec = [](uint64_t qpath, uint64_t cookie, const std::string& name,
                  int claim = -1) {
        p9::bytes b;
        b.push_back(0);
        put(b, 0, 4);
        put(b, qpath, 8);
        put(b, cookie, 8);
        b.push_back(8);
        put(b, claim < 0 ? name.size() : size_t(claim), 2);
        b.insert(b.end(), name.begin(), name.end());
        return b;
    };
    auto parse = [&](const p9::bytes& b, p9::dirent_rec& e) {
        uint8_t* at = m + pg - b.size();
        memcpy(at, b.data(), b.size());
        return p9::parse_dirent(at, b.size(), e);
    };
    p9::dirent_rec e;
    require(parse(rec(7, 9, "name"), e) == 0 && e.qpath == 7 && e.cookie == 9
            && e.type == 8 && e.len == 4 && !memcmp(e.name, "name", 4)
            && e.size == 28, "dirent fields");
    require(parse(rec(1, INT64_MAX, std::string(255, 'x')), e) == 0
            && e.len == 255, "255-byte name, cookie INT64_MAX");
    // These would read past the record; the guard page faults if so.
    require(parse(rec(1, 1, "f", 40), e) == EIO, "name runs past the reply");
    require(parse(rec(1, 1, "", 0), e) == EIO, "empty name");
    p9::bytes hdr = rec(1, 1, "f");
    hdr.resize(23);
    require(parse(hdr, e) == EIO, "record shorter than its header");
    require(parse(rec(1, 1, std::string(256, 'x')), e) == EIO,
            "256-byte name");
    require(parse(rec(1, 1, "a/b"), e) == EIO, "slash in a name");
    require(parse(rec(1, 1, std::string("a\0b", 3)), e) == EIO,
            "NUL in a name");
    require(parse(rec(1, uint64_t(INT64_MAX) + 1, "f"), e) == EIO,
            "cookie above INT64_MAX");
    munmap(m, 2 * pg);
    puts("PASS: Rreaddir records");
}

int main(int argc, char** argv)
{
    mkdir("/tmp", 0755);
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode.empty()) {
        options();
        qids();
        attrs();
        dirents();
    } else if (mode == "diod" && argc == 6) {
        diod(argv[2], argv[3], strtoul(argv[4], nullptr, 10), argv[5]);
        files(argv[2], argv[3], strtoul(argv[4], nullptr, 10), argv[5]);
        changes(argv[2], argv[3], strtoul(argv[4], nullptr, 10), argv[5]);
    } else if (mode == "peer" && argc == 4) {
        peer(argv[2], argv[3]);
    } else {
        require(false, "known mode and argument count");
    }
    printf("SUMMARY: %u fs checks, 0 failures\n", checks);
    return 0;
}
