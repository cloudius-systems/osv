/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Mount lifetime and unmount admission, exercised on private ramfs mounts.
//
// An unmount that succeeds while the mount still has users frees the mount
// under them, so a case that observes such a success reports FAIL and then
// deliberately leaks its users instead of touching the freed mount again.

#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <osv/dentry.h>
#include <osv/mount.h>
#include <osv/vnode.h>

extern "C" int namei(const char *path, struct dentry **dpp);
extern "C" void drele(struct dentry *dp);
extern "C" int vfs_findroot(const char *path, struct mount **mp, char **root);
// Weak so the test still loads on a kernel without it and reports the
// unpinned lookup as a failure.
extern "C" void vfs_putroot(struct mount *mp) __attribute__((weak));

static int tests = 0, fails = 0;

static void report(bool ok, const std::string& msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg.c_str());
}

static bool mount_ramfs(const std::string& dir)
{
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        report(false, "mkdir " + dir + ": " + strerror(errno));
        return false;
    }
    if (mount("none", dir.c_str(), "ramfs", 0, nullptr) != 0) {
        report(false, "mount " + dir + ": " + strerror(errno));
        return false;
    }
    return true;
}

// Expect EBUSY. Returns false if the unmount went ahead, in which case the
// caller must not touch the mount or its users again.
static bool expect_busy(const std::string& dir, const std::string& what)
{
    int r = umount(dir.c_str());
    int err = errno;
    if (r == 0) {
        report(false, what + ": unmount returned 0 with users remaining");
        return false;
    }
    report(err == EBUSY, what + ": unmount fails with EBUSY (got " +
           strerror(err) + ")");
    return true;
}

static void expect_unmount(const std::string& dir, const std::string& what)
{
    int r = umount(dir.c_str());
    report(r == 0, what + ": unmount succeeds" +
           (r ? std::string(" (got ") + strerror(errno) + ")" : ""));
    if (r == 0) {
        rmdir(dir.c_str());
    }
}

static bool write_file(const std::string& path, const char *data)
{
    int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        return false;
    }
    bool ok = write(fd, data, strlen(data)) == (ssize_t)strlen(data);
    return close(fd) == 0 && ok;
}

// (d) No-regression: idle mounts unmount, repeatedly.
static void test_cycles()
{
    const std::string dir = "/tmp/vfs-pins-d";
    bool ok = true;
    int i;
    for (i = 0; i < 50 && ok; i++) {
        if (!mount_ramfs(dir)) {
            return;
        }
        ok = write_file(dir + "/f", "cycle");
        int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
        struct stat st;
        ok = ok && dfd >= 0 && close(dfd) == 0 &&
             stat((dir + "/f").c_str(), &st) == 0 && st.st_size == 5;
        ok = umount(dir.c_str()) == 0 && ok;
    }
    rmdir(dir.c_str());
    report(ok, "(d) 50 mount/use/close/unmount cycles (" +
           std::to_string(i) + " done)");
}

// No-regression: a filesystem that refuses unmount leaves the mount listed
// and usable.
static int refuse_unmount(struct mount *, int)
{
    return EIO;
}

static void test_fs_refusal()
{
    const std::string dir = "/tmp/vfs-pins-f";
    if (!mount_ramfs(dir)) {
        return;
    }
    struct dentry *dp;
    if (namei(dir.c_str(), &dp) != 0) {
        report(false, "(f) namei of mount root");
        return;
    }
    struct mount *mp = dp->d_mount;
    drele(dp);
    struct vfsops *orig = mp->m_op;
    struct vfsops refusing = *orig;
    refusing.vfs_unmount = refuse_unmount;
    mp->m_op = &refusing;
    int r = umount(dir.c_str());
    int err = errno;
    mp->m_op = orig;
    report(r == -1 && err == EIO, "(f) filesystem refusal is returned");
    report(write_file(dir + "/after", "ok"), "(f) mount usable after refusal");
    expect_unmount(dir, "(f) after refusal");
}

// (a) A file under the mount is open.
static void test_open_file()
{
    const std::string dir = "/tmp/vfs-pins-a";
    if (!mount_ramfs(dir)) {
        return;
    }
    int fd = open((dir + "/f").c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0 || write(fd, "abc", 3) != 3) {
        report(false, "(a) setup");
        return;
    }
    if (!expect_busy(dir, "(a) open file")) {
        return;
    }
    char buf[8] = {};
    bool ok = pwrite(fd, "xyz", 3, 3) == 3 && pread(fd, buf, 6, 0) == 6 &&
              memcmp(buf, "abcxyz", 6) == 0;
    ok = ok && write_file(dir + "/g", "more");
    struct stat st;
    ok = ok && stat((dir + "/g").c_str(), &st) == 0 && st.st_size == 4;
    report(ok, "(a) mount usable after EBUSY");
    report(close(fd) == 0, "(a) close");
    expect_unmount(dir, "(a) after close");
}

// (b) Only the mount root directory is open.
static void test_root_open()
{
    const std::string dir = "/tmp/vfs-pins-b";
    if (!mount_ramfs(dir)) {
        return;
    }
    int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd < 0) {
        report(false, "(b) setup");
        return;
    }
    if (!expect_busy(dir, "(b) root directory open")) {
        return;
    }
    report(close(dfd) == 0, "(b) close root directory");
    expect_unmount(dir, "(b) after root close");
}

// (e) A file mapping outlives its descriptor.
static void test_mapping()
{
    const std::string dir = "/tmp/vfs-pins-e";
    if (!mount_ramfs(dir)) {
        return;
    }
    std::string page(4096, 'm');
    int fd = open((dir + "/m").c_str(), O_CREAT | O_RDWR, 0644);
    void *p = MAP_FAILED;
    if (fd >= 0 && write(fd, page.data(), page.size()) == (ssize_t)page.size()) {
        p = mmap(nullptr, page.size(), PROT_READ, MAP_PRIVATE, fd, 0);
    }
    if (fd < 0 || p == MAP_FAILED || close(fd) != 0) {
        report(false, "(e) setup");
        return;
    }
    if (!expect_busy(dir, "(e) mapping after close")) {
        return;
    }
    report(*(volatile char *)p == 'm', "(e) mapping readable after EBUSY");
    report(munmap(p, page.size()) == 0, "(e) munmap");
    expect_unmount(dir, "(e) after munmap");
}

// (c) A lookup parked in VOP_LOOKUP holds the mount through its dentry
// chain. The test swaps the mount root's vnode operations for a copy whose
// lookup parks on "missing", so the unmount runs deterministically while
// the lookup is inside the filesystem. By then the lookup already holds a
// dentry of the mount (namei() holds m_root, and lstat()'s lookup() holds
// the parent), so this case does not depend on the vfs_findroot() pin;
// (p) tests the pin. The windows that rely on the pin alone (namei()
// between vfs_findroot() and dref(ddp), and namei_last_nofollow() on a
// mount-point path) are not raced here: that would need a hook in the
// lookup path.
static struct vnops *orig_vnops;
static struct vnops parking_vnops;
static std::mutex park_mtx;
static std::condition_variable park_cv;
static bool parked, released;
// Static: a lookup left parked by a failed case must not write to a dead
// stack frame if it ever resumes.
static int lookup_result;

static int parking_lookup(struct vnode *dvp, char *name, struct vnode **vpp)
{
    if (!strcmp(name, "missing")) {
        std::unique_lock<std::mutex> lk(park_mtx);
        parked = true;
        park_cv.notify_all();
        park_cv.wait(lk, [] { return released; });
    }
    return orig_vnops->vop_lookup(dvp, name, vpp);
}

// Returns false if the mount was freed under the parked lookup.
static bool lookup_case(const std::string& dir, bool use_lstat,
                        const std::string& what)
{
    {
        std::lock_guard<std::mutex> lk(park_mtx);
        parked = released = false;
    }
    lookup_result = 0;
    std::string path = dir + "/missing";
    auto t = new std::thread([path, use_lstat] {
        struct stat st;
        int r = use_lstat ? lstat(path.c_str(), &st) : stat(path.c_str(), &st);
        lookup_result = r ? errno : 0;
    });
    // Poll rather than condition_variable::wait_for(): libstdc++ implements
    // it with pthread_cond_clockwait(), which OSv stubs out with EINVAL.
    bool entered = false;
    for (int i = 0; i < 1000 && !entered; i++) {
        {
            std::lock_guard<std::mutex> lk(park_mtx);
            entered = parked;
        }
        if (!entered) {
            usleep(10000);
        }
    }
    if (!entered) {
        report(false, what + ": lookup did not reach the filesystem");
        t->detach();
        return false;
    }
    if (!expect_busy(dir, what)) {
        // The parked lookup now references a freed mount; leave it parked.
        t->detach();
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(park_mtx);
        released = true;
        park_cv.notify_all();
    }
    t->join();
    delete t;
    report(lookup_result == ENOENT, what + ": lookup completes with ENOENT");
    return true;
}

static void test_lookup_in_flight()
{
    const std::string dir = "/tmp/vfs-pins-c";
    if (!mount_ramfs(dir)) {
        return;
    }
    struct dentry *dp;
    if (namei(dir.c_str(), &dp) != 0) {
        report(false, "(c) namei of mount root");
        return;
    }
    struct vnode *root = dp->d_vnode;
    orig_vnops = root->v_op;
    parking_vnops = *orig_vnops;
    parking_vnops.vop_lookup = parking_lookup;
    root->v_op = &parking_vnops;
    drele(dp);

    if (!lookup_case(dir, false, "(c1) stat lookup in flight (namei)") ||
        !lookup_case(dir, true,
                     "(c2) lstat lookup in flight (namei_last_nofollow)")) {
        return;
    }
    root->v_op = orig_vnops;
    expect_unmount(dir, "(c) after lookups complete");
}

// (p) The mount returned by vfs_findroot() stays pinned until released.
static void test_findroot_pin()
{
    const std::string dir = "/tmp/vfs-pins-p";
    if (!mount_ramfs(dir)) {
        return;
    }
    struct mount *mp;
    char *rest;
    std::string path = dir + "/x";
    if (vfs_findroot(path.c_str(), &mp, &rest) != 0) {
        report(false, "(p) vfs_findroot");
        return;
    }
    if (!expect_busy(dir, "(p) vfs_findroot pin held")) {
        return;
    }
    if (!vfs_putroot) {
        report(false, "(p) vfs_putroot missing");
        return;
    }
    vfs_putroot(mp);
    expect_unmount(dir, "(p) after vfs_putroot");
}

// (g) No-regression: every lookup exit path releases what it took.
static void test_lookup_exits()
{
    const std::string dir = "/tmp/vfs-pins-g";
    if (!mount_ramfs(dir)) {
        return;
    }
    struct stat st;
    bool ok = write_file(dir + "/f", "f") &&
              mkdir((dir + "/sub").c_str(), 0755) == 0 &&
              symlink("l2", (dir + "/l1").c_str()) == 0 &&
              symlink("l1", (dir + "/l2").c_str()) == 0 &&
              symlink("f", (dir + "/lf").c_str()) == 0;
    report(ok, "(g) setup");
    auto fails_with = [&](int r, int err) { return r == -1 && errno == err; };
    report(fails_with(stat((dir + "/missing").c_str(), &st), ENOENT),
           "(g) namei ENOENT");
    report(fails_with(stat((dir + "/f/x").c_str(), &st), ENOTDIR),
           "(g) namei ENOTDIR");
    report(fails_with(stat((dir + "/l1").c_str(), &st), ELOOP),
           "(g) namei ELOOP");
    report(stat((dir + "/lf").c_str(), &st) == 0 && st.st_size == 1,
           "(g) namei through symlink");
    // Hits need a held dentry: the mount root always is, sub while open.
    int sfd = open((dir + "/sub").c_str(), O_RDONLY | O_DIRECTORY);
    report(stat(dir.c_str(), &st) == 0 && sfd >= 0 &&
           stat((dir + "/sub").c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
           close(sfd) == 0, "(g) namei cache hit");
    report(fails_with(lstat((dir + "/missing").c_str(), &st), ENOENT),
           "(g) namei_last_nofollow ENOENT");
    report(lstat((dir + "/l1").c_str(), &st) == 0 && S_ISLNK(st.st_mode),
           "(g) namei_last_nofollow symlink");
    report(lstat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode),
           "(g) namei_last_nofollow mount root");
    report(fails_with(open((dir + "/l1").c_str(), O_RDONLY | O_NOFOLLOW),
                      ELOOP), "(g) O_NOFOLLOW ELOOP");
    expect_unmount(dir, "(g) after failed and successful lookups");
}

// (h) A filesystem mounted inside the mount.
static void test_submount()
{
    const std::string dir = "/tmp/vfs-pins-h", sub = dir + "/sub";
    if (!mount_ramfs(dir) || !mount_ramfs(sub)) {
        return;
    }
    if (!expect_busy(dir, "(h) submount present")) {
        return;
    }
    expect_unmount(sub, "(h) submount");
    expect_unmount(dir, "(h) after submount unmounted");
}

// (b2) The mount root is the current directory. Runs last: on failure the
// cwd is left in the freed mount rather than released into it.
static void test_cwd()
{
    const std::string dir = "/tmp/vfs-pins-w";
    char saved[PATH_MAX];
    if (!getcwd(saved, sizeof(saved)) || !mount_ramfs(dir)) {
        report(false, "(b2) setup");
        return;
    }
    if (chdir(dir.c_str()) != 0) {
        report(false, "(b2) chdir");
        return;
    }
    if (!expect_busy(dir, "(b2) mount root is cwd")) {
        return;
    }
    report(chdir(saved) == 0, "(b2) chdir back");
    expect_unmount(dir, "(b2) after chdir back");
}

// An optional argument selects cases by letter, so a kernel that frees busy
// mounts can be observed one case per boot.
int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    const struct {
        char tag;
        void (*fn)();
    } cases[] = {
        {'d', test_cycles}, {'g', test_lookup_exits}, {'f', test_fs_refusal},
        {'a', test_open_file}, {'b', test_root_open}, {'e', test_mapping},
        {'p', test_findroot_pin}, {'c', test_lookup_in_flight},
        {'h', test_submount}, {'w', test_cwd},
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
