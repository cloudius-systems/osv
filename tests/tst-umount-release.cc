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
//
// A case that observes a leaked or double-dropped reference reports FAIL and
// then leaks what it holds rather than touch freed state again.

#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
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
                        const char *dev, const char *fs, unsigned long flags)
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

// An optional argument selects cases by letter.
int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    const struct {
        char tag;
        void (*fn)();
    } cases[] = {
        {'c', test_ramfs}, {'r', test_rofs},
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
