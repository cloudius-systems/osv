/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-rename-failure

#include "tst-fs.hh"
#include <osv/dentry.h>
#include <osv/mount.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
int namei(const char *, struct dentry **);
struct dentry *dentry_lookup(struct mount *, char *);
}

namespace {

dentry_ref lookup_ref(const fs::path& path)
{
    struct dentry *dp = nullptr;
    BOOST_REQUIRE_EQUAL(namei(path.c_str(), &dp), 0);
    return dentry_ref(dp, false);
}

// No filesystem registration or global ramfs operations are changed. Each
// test owns a private mount, and only its root vnode dispatch is overridden.
class private_ramfs {
public:
    TempDir dir;

    private_ramfs()
    {
        BOOST_REQUIRE_EQUAL(mount("", dir.c_str(), "ramfs", 0, nullptr), 0);
    }

    ~private_ramfs()
    {
        boost::system::error_code ec;
        // Test locals declared after this fixture have already released
        // their dentries; ramfs does not keep an unlinked node alive merely
        // because a dentry references it, so remove backend nodes now.
        fs::remove_all(dir / "old", ec);
        BOOST_CHECK_MESSAGE(!ec, "private ramfs old cleanup: " << ec.message());
        fs::remove_all(dir / "target", ec);
        BOOST_CHECK_MESSAGE(!ec, "private ramfs cleanup: " << ec.message());
        // Do not let TempDir recurse into a still-mounted filesystem.
        if (umount(dir.c_str()) != 0) {
            std::fprintf(stderr, "FAIL: private ramfs unmount: %d\n", errno);
            std::abort();
        }
    }
};

int deny_rename(struct vnode *dvp1, struct vnode *vp1, char *name1,
                struct vnode *dvp2, struct vnode *vp2, char *name2)
{
    // sys_rename dispatches via dvp1, not vp1 or vp2. Both names in this
    // fixture are direct children of the same mount root. EACCES proves
    // that this callback was reached with the intended arguments.
    if (dvp1 != dvp2 || dvp1 != dvp1->v_mount->m_root->d_vnode ||
        !vp2 || vp1 == vp2 || vp1->v_type != VREG || vp2->v_type != VREG ||
        std::strcmp(name1, "old") || std::strcmp(name2, "target")) {
        return EINVAL;
    }
    return EACCES;
}

class deny_root_rename {
    dentry_ref root;
    struct vnops *original;
    struct vnops ops;
public:
    explicit deny_root_rename(const fs::path& path)
        : root(lookup_ref(path))
    {
        auto vp = root->d_vnode;
        vn_lock(vp);
        original = vp->v_op;
        ops = *original;
        ops.vop_rename = deny_rename;
        vp->v_op = &ops;
        vn_unlock(vp);
    }

    ~deny_root_rename()
    {
        auto vp = root->d_vnode;
        vn_lock(vp);
        vp->v_op = original;
        vn_unlock(vp);
        // root pins the vnode until after restoration; ops stays alive until
        // then, including when a BOOST_REQUIRE unwinds the test body.
    }

    deny_root_rename(const deny_root_rename&) = delete;
    deny_root_rename& operator=(const deny_root_rename&) = delete;
};

void create_file(const fs::path& path)
{
    int fd = open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    BOOST_REQUIRE(fd >= 0);
    BOOST_REQUIRE_EQUAL(close(fd), 0);
}

ino_t inode(const fs::path& path)
{
    struct stat st;
    BOOST_REQUIRE_EQUAL(stat(path.c_str(), &st), 0);
    return st.st_ino;
}

void check_cached(const dentry_ref& before, const std::string& fullpath)
{
    BOOST_CHECK_EQUAL(before->d_path, fullpath);
    // Query the actual cache, not namei's backend fallback: a fresh ramfs
    // lookup can return the same inode and conceal the erroneous cache move.
    std::string path = fullpath;
    dentry_ref after(dentry_lookup(before->d_mount, &path[0]), false);
    BOOST_CHECK_MESSAGE(after.get() == before.get(), "cached identity for " << fullpath);
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE(rename_failure_preserves_names)
{
    std::puts("Running rename_failure_preserves_names");
    private_ramfs fs;
    auto old = fs.dir / "old";
    auto target = fs.dir / "target";
    create_file(old);
    create_file(target);
    auto old_dp = lookup_ref(old);
    auto target_dp = lookup_ref(target);
    const std::string old_path = old_dp->d_path;
    const std::string target_path = target_dp->d_path;
    auto *const old_vp = old_dp->d_vnode;
    auto *const target_vp = target_dp->d_vnode;
    const auto old_ino = inode(old);
    const auto target_ino = inode(target);
    BOOST_REQUIRE(old_ino != target_ino);
    BOOST_REQUIRE_EQUAL(old_path, "/old");
    BOOST_REQUIRE_EQUAL(target_path, "/target");
    check_cached(old_dp, old_path);
    check_cached(target_dp, target_path);
    BOOST_REQUIRE_EQUAL(access(fs.dir.c_str(), W_OK), 0);

    {
        deny_root_rename deny(fs.dir);
        errno = 0;
        int rc = rename(old.c_str(), target.c_str());
        int error = errno;
        BOOST_REQUIRE_EQUAL(rc, -1);
        BOOST_REQUIRE_EQUAL(error, EACCES);
    }

    check_cached(old_dp, old_path);
    check_cached(target_dp, target_path);
    BOOST_CHECK(old_dp->d_vnode == old_vp);
    BOOST_CHECK(target_dp->d_vnode == target_vp);
    BOOST_CHECK_EQUAL(inode(old), old_ino);
    BOOST_CHECK_EQUAL(inode(target), target_ino);
}

BOOST_AUTO_TEST_CASE(rename_success_replaces_target)
{
    std::puts("Running rename_success_replaces_target");
    private_ramfs fs;
    auto old = fs.dir / "old";
    auto target = fs.dir / "target";
    create_file(old);
    create_file(target);
    auto old_dp = lookup_ref(old);
    auto target_dp = lookup_ref(target);
    const auto old_ino = inode(old);
    BOOST_REQUIRE(old_ino != inode(target));
    {
        deny_root_rename deny(fs.dir);
        // Restore dispatch without issuing a denied rename: the positive
        // control is independent of damage left by the baseline failure.
    }
    BOOST_REQUIRE_EQUAL(rename(old.c_str(), target.c_str()), 0);
    struct stat st;
    errno = 0;
    int rc = stat(old.c_str(), &st);
    int error = errno;
    BOOST_CHECK_EQUAL(rc, -1);
    BOOST_CHECK_EQUAL(error, ENOENT);
    BOOST_CHECK_EQUAL(inode(target), old_ino);
    BOOST_CHECK(lookup_ref(target)->d_vnode == old_dp->d_vnode);
}
