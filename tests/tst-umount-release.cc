/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// An unmount must drop the mount's references on its root dentry and on the
// dentry it covers (release_mp_dentries()), and a refused unmount must drop
// neither.
//
// (c) ramfs, the control: its unmount already releases them.
// (r) rofs, on a private in-memory block device holding a minimal image.
// (i) bsd ZFS: an idle unforced unmount succeeds and releases both.
// (h) bsd ZFS: the same with one extra hold on the mount, which zfs_umount()
//     allows.
// (z) bsd ZFS: an unforced unmount refused with EBUSY, then a real one.
// (d) bsd ZFS: the same with only the mount root directory open.
// (v) bsd ZFS: the same with a bare reference on the root vnode.
// (w) bsd ZFS: the same with a bare reference on a file vnode.
// The zfs cases run only when the root filesystem is bsd ZFS; they mount the
// existing osv/zfs dataset, which the image builder creates and the loader
// leaves unmounted.
//
// A case that observes a leaked or double-dropped reference reports FAIL and
// then leaks what it holds rather than touch freed state again.

#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include <osv/bio.h>
#include <osv/dentry.h>
#include <osv/device.h>
#include <osv/mount.h>
#include <osv/vnode.h>
#include <fs/rofs/rofs.hh>

extern "C" int namei(const char *path, struct dentry **dpp);
extern "C" struct dentry *dentry_lookup(struct mount *mp, char *path);

static int tests = 0, fails = 0;

static bool report(bool ok, const std::string& msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg.c_str());
    return ok;
}

static std::string n2s(long n)
{
    return std::to_string(n);
}

// The mount root dentry of the mount at dir, and its d_refcnt not counting
// the reference this lookup takes.
static int root_refs(const std::string& dir, struct mount **mpp)
{
    struct dentry *dp;
    if (namei(dir.c_str(), &dp) != 0) {
        return -1;
    }
    int n = dp->d_refcnt - 1;
    if (mpp) {
        *mpp = dp->d_mount;
    }
    drele(dp);
    return n;
}

// Is root, the root dentry of the freed mount mp, still in the dentry hash?
// dentry_lookup() compares and hashes the mount pointer without
// dereferencing it, so this is safe after the mount is freed. A hit that is
// not root is a dentry leaked by an earlier mount at the same address.
static bool root_hashed(struct mount *mp, struct dentry *root)
{
    char path[] = "/";
    struct dentry *dp = dentry_lookup(mp, path);
    if (!dp) {
        return false;
    }
    bool hashed = dp == root;
    // A leaked dentry has d_refcnt >= 1 besides ours: this cannot free it.
    drele(dp);
    return hashed;
}

// Mount, check the covered dentry gained exactly the mount's reference,
// unmount, and check that both references are gone.
static void mount_cycle(const std::string& what, const std::string& dir,
                        const char *dev, const char *fs, unsigned long flags,
                        void (*after_mount)(struct mount *) = nullptr)
{
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        report(false, what + ": mkdir " + dir + ": " + strerror(errno));
        return;
    }
    struct dentry *cov;
    if (namei(dir.c_str(), &cov) != 0) {
        report(false, what + ": namei " + dir);
        return;
    }
    int c0 = cov->d_refcnt;
    if (mount(dev, dir.c_str(), fs, flags, nullptr) != 0) {
        report(false, what + ": mount: " + strerror(errno));
        drele(cov);
        return;
    }
    int c1 = cov->d_refcnt;
    struct mount *mp = nullptr;
    int r1 = root_refs(dir, &mp);
    struct dentry *root = mp ? mp->m_root : nullptr;
    report(c1 == c0 + 1, what + ": mount holds the covered dentry (" +
           n2s(c0) + " -> " + n2s(c1) + ")");
    report(r1 == 1, what + ": mount holds its root dentry (" + n2s(r1) + ")");
    if (after_mount) {
        after_mount(mp);
    }
    if (umount(dir.c_str()) != 0) {
        report(false, what + ": unmount: " + strerror(errno));
        drele(cov);
        return;
    }
    int c2 = cov->d_refcnt;
    bool ok = report(c2 == c0, what + ": unmount releases the covered dentry (" +
                     n2s(c0) + " -> " + n2s(c1) + " -> " + n2s(c2) + ")");
    ok = report(!root_hashed(mp, root), what + ": unmount releases its root dentry")
         && ok;
    drele(cov);
    if (ok) {
        rmdir(dir.c_str());
    }
}

static void test_ramfs()
{
    mount_cycle("(c) ramfs", "/tmp/umrel-c", "none", "ramfs", 0);
}

// A minimal rofs image: the superblock in block 0 and, in block 1, the
// structure info holding a single inode, an empty root directory.
static char rofs_image[2 * BSIZE];

static void ram_strategy(struct bio *bio)
{
    bool ok = bio->bio_cmd == BIO_READ && bio->bio_offset >= 0 &&
              (size_t)bio->bio_offset + bio->bio_bcount <= sizeof(rofs_image);
    if (ok) {
        memcpy(bio->bio_data, rofs_image + bio->bio_offset, bio->bio_bcount);
    }
    biodone(bio, ok);
}

static struct devops ram_devops = {
    no_open, no_close, no_read, no_write, no_ioctl, no_devctl, ram_strategy,
};

static struct driver ram_driver = {
    "umrel", &ram_devops, 0, 0,
};

static void test_rofs()
{
    auto sb = reinterpret_cast<rofs_super_block *>(rofs_image);
    sb->magic = ROFS_MAGIC;
    sb->version = ROFS_VERSION;
    sb->block_size = BSIZE;
    sb->structure_info_first_block = 1;
    sb->structure_info_blocks_count = 1;
    sb->directory_entries_count = 0;
    sb->symlinks_count = 0;
    sb->inodes_count = 1;
    auto root = reinterpret_cast<rofs_inode *>(rofs_image + BSIZE);
    root->mode = S_IFDIR | 0555;
    root->inode_no = 1;
    root->data_offset = 0;
    root->dir_children_count = 0;

    struct device *dev = device_create(&ram_driver, "umrel0", D_BLK);
    if (!dev) {
        report(false, "(r) rofs: device_create");
        return;
    }
    dev->size = sizeof(rofs_image);
    mount_cycle("(r) rofs", "/tmp/umrel-r", "/dev/umrel0", "rofs", MS_RDONLY);
    // Not destroyed: on a failed case the mount may still be listed.
}

// The zfs cases expect bsd ZFS's unmount, which refuses a busy mount.
// OpenZFS's does not (it relies on the VFS for that), so they are skipped.
static bool root_is_zfs()
{
#ifdef CONF_ZFS_OPENZFS
    return false;
#endif
    for (auto& m : osv::current_mounts()) {
        if (m.path == "/" && m.type == "zfs") {
            return true;
        }
    }
    return false;
}

static void test_zfs_idle()
{
    if (!root_is_zfs()) {
        printf("SKIP: (i) zfs: root filesystem is not bsd zfs\n");
        return;
    }
    mount_cycle("(i) zfs idle", "/tmp/umrel-i", "osv/zfs", "zfs", 0);
}

// (h) One extra hold on the mount itself (vfs_busy(), as zfs_ioctl.c's
// VFS_HOLD() takes): zfs_umount() allows one ("off by 1"), so this idle
// unmount succeeds, as it did when the check ran after the release.
static void test_zfs_vfs_hold()
{
    if (!root_is_zfs()) {
        printf("SKIP: (h) zfs: root filesystem is not bsd zfs\n");
        return;
    }
    // The mount is freed by the unmount; the hold is never dropped.
    mount_cycle("(h) zfs one vfs hold", "/tmp/umrel-h", "osv/zfs", "zfs", 0,
                [](struct mount *mp) { vfs_busy(mp); });
}

static void test_zfs()
{
    const std::string what = "(z) zfs", dir = "/tmp/umrel-z";
    const char *ds = "osv/zfs";
    if (!root_is_zfs()) {
        printf("SKIP: %s: root filesystem is not bsd zfs\n", what.c_str());
        return;
    }
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        report(false, what + ": mkdir " + dir + ": " + strerror(errno));
        return;
    }
    struct dentry *cov;
    if (namei(dir.c_str(), &cov) != 0) {
        report(false, what + ": namei " + dir);
        return;
    }
    int c0 = cov->d_refcnt;
    if (mount(ds, dir.c_str(), "zfs", 0, nullptr) != 0) {
        report(false, what + ": mount " + ds + ": " + strerror(errno));
        drele(cov);
        return;
    }
    int c1 = cov->d_refcnt;
    report(c1 == c0 + 1, what + ": mount holds the covered dentry (" +
           n2s(c0) + " -> " + n2s(c1) + ")");

    const std::string f = dir + "/f";
    int fd = open(f.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0 || write(fd, "abc", 3) != 3) {
        report(false, what + ": create " + f + ": " + strerror(errno));
        return;
    }
    struct mount *mp = nullptr;
    int r0 = root_refs(dir, &mp);
    struct dentry *root = mp ? mp->m_root : nullptr;

    int r = umount(dir.c_str());
    int err = errno;
    if (r == 0) {
        // The mount is gone under the open file: leak fd and cov.
        report(false, what + ": unforced unmount returned 0 with a file open");
        return;
    }
    report(err == EBUSY, what + ": unforced unmount with a file open fails "
           "with EBUSY (got " + strerror(err) + ")");
    int r1 = root_refs(dir, nullptr);
    int c2 = cov->d_refcnt;
    bool kept = report(r1 == r0, what + ": refused unmount keeps the root "
                       "dentry reference (" + n2s(r0) + " -> " + n2s(r1) + ")");
    kept = report(c2 == c1, what + ": refused unmount keeps the covered "
                  "dentry reference (" + n2s(c1) + " -> " + n2s(c2) + ")")
           && kept;
    if (!kept) {
        // A real unmount would release the dropped references a second
        // time: leave the mount, fd and cov as they are.
        return;
    }

    char buf[8] = {};
    struct stat st;
    const std::string g = dir + "/g";
    int gfd = open(g.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    bool ok = pwrite(fd, "xyz", 3, 3) == 3 && pread(fd, buf, 6, 0) == 6 &&
              memcmp(buf, "abcxyz", 6) == 0 && gfd >= 0 &&
              write(gfd, "more", 4) == 4 && close(gfd) == 0 &&
              stat(g.c_str(), &st) == 0 && st.st_size == 4;
    report(ok, what + ": mount usable after EBUSY");
    unlink(g.c_str());
    unlink(f.c_str());
    report(close(fd) == 0, what + ": close");

    r = umount(dir.c_str());
    if (!report(r == 0, what + ": unmount after close" +
                (r ? std::string(" (got ") + strerror(errno) + ")" : ""))) {
        return;
    }
    int c3 = cov->d_refcnt;
    ok = report(c3 == c0, what + ": unmount releases the covered dentry (" +
                n2s(c0) + " -> " + n2s(c3) + ")");
    ok = report(!root_hashed(mp, root), what + ": unmount releases its root dentry")
         && ok;
    drele(cov);
    if (ok) {
        rmdir(dir.c_str());
    }
}

// Mount osv/zfs at dir, hold what hold() takes, and expect an unforced
// unmount to be refused with both dentry references intact; then drop the
// hold and expect a clean unmount. hold() returns false on setup failure.
static void busy_cycle(const std::string& what, const std::string& dir,
                       bool (*hold)(const std::string& dir),
                       void (*unhold)())
{
    if (!root_is_zfs()) {
        printf("SKIP: %s: root filesystem is not bsd zfs\n", what.c_str());
        return;
    }
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        report(false, what + ": mkdir " + dir + ": " + strerror(errno));
        return;
    }
    struct dentry *cov;
    if (namei(dir.c_str(), &cov) != 0) {
        report(false, what + ": namei " + dir);
        return;
    }
    int c0 = cov->d_refcnt;
    if (mount("osv/zfs", dir.c_str(), "zfs", 0, nullptr) != 0) {
        report(false, what + ": mount: " + strerror(errno));
        drele(cov);
        return;
    }
    int c1 = cov->d_refcnt;
    if (!report(hold(dir), what + ": hold")) {
        return;
    }
    struct mount *mp = nullptr;
    int r0 = root_refs(dir, &mp);
    struct dentry *root = mp ? mp->m_root : nullptr;
    int r = umount(dir.c_str());
    int err = errno;
    if (r == 0) {
        // The mount is gone under the hold: leak it and cov.
        report(false, what + ": unforced unmount returned 0 while held");
        return;
    }
    report(err == EBUSY, what + ": unforced unmount while held fails with "
           "EBUSY (got " + strerror(err) + ")");
    // The covered dentry belongs to the parent mount, so it is safe to read
    // even if the refused unmount dropped (and freed) the root dentry.
    if (!report(cov->d_refcnt == c1, what + ": refused unmount keeps the "
                "covered dentry reference (" + n2s(c1) + " -> " +
                n2s(cov->d_refcnt) + ")")) {
        return;
    }
    int r1 = root_refs(dir, nullptr);
    if (!report(r1 == r0, what + ": refused unmount keeps the root dentry "
                "reference (" + n2s(r0) + " -> " + n2s(r1) + ")")) {
        return;
    }
    unhold();
    unlink((dir + "/f").c_str());
    r = umount(dir.c_str());
    if (!report(r == 0, what + ": unmount after release" +
                (r ? std::string(" (got ") + strerror(errno) + ")" : ""))) {
        return;
    }
    bool ok = report(cov->d_refcnt == c0, what + ": unmount releases the "
                     "covered dentry (" + n2s(c0) + " -> " +
                     n2s(cov->d_refcnt) + ")");
    ok = report(!root_hashed(mp, root), what + ": unmount releases its root dentry")
         && ok;
    drele(cov);
    if (ok) {
        rmdir(dir.c_str());
    }
}

// (d) The mount root directory is open: the root dentry has a second
// reference, the root vnode only the root dentry's.
static int held_fd = -1;
static bool hold_root_dir(const std::string& dir)
{
    held_fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    return held_fd >= 0;
}
static void unhold_fd()
{
    close(held_fd);
}

// (v) A bare reference on the root vnode, as the page cache takes on a file
// it caches: the root dentry has only the mount's reference.
// (w) The same on a file vnode, with no dentry left for the file.
static struct vnode *held_vp;
static bool hold_vnode(const std::string& path)
{
    struct dentry *dp;
    if (namei(path.c_str(), &dp) != 0) {
        return false;
    }
    held_vp = dp->d_vnode;
    vref(held_vp);
    drele(dp);
    return true;
}
static bool hold_root_vnode(const std::string& dir)
{
    return hold_vnode(dir) && held_vp->v_refcnt == 2;
}
static bool hold_file_vnode(const std::string& dir)
{
    const std::string f = dir + "/f";
    int fd = open(f.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    return fd >= 0 && close(fd) == 0 && hold_vnode(f) &&
           held_vp->v_refcnt == 1;
}
static void unhold_vnode()
{
    vrele(held_vp);
}

static void test_zfs_root_dir_open()
{
    busy_cycle("(d) zfs root directory open", "/tmp/umrel-d",
               hold_root_dir, unhold_fd);
}

static void test_zfs_root_vnode()
{
    busy_cycle("(v) zfs root vnode held", "/tmp/umrel-v",
               hold_root_vnode, unhold_vnode);
}

static void test_zfs_file_vnode()
{
    busy_cycle("(w) zfs file vnode held", "/tmp/umrel-w",
               hold_file_vnode, unhold_vnode);
}

// An optional argument selects cases by letter.
int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    const struct {
        char tag;
        void (*fn)();
    } cases[] = {
        {'c', test_ramfs}, {'r', test_rofs}, {'i', test_zfs_idle},
        {'h', test_zfs_vfs_hold},
        {'z', test_zfs}, {'d', test_zfs_root_dir_open},
        {'v', test_zfs_root_vnode}, {'w', test_zfs_file_vnode},
    };
    mkdir("/tmp", 0755);
    for (auto& c : cases) {
        if (!only || strchr(only, c.tag)) {
            c.fn();
        }
    }
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
