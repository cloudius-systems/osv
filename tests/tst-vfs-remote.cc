/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Optional VFS hooks for filesystems whose open is authorized by a server,
// exercised on private ramfs mounts whose vnode operations are wrapped by a
// small synthetic fixture.
//
// (c) create/open handoff: vop_create_open creates and opens in one step.
//     The fixture plays an external client that renames the new object away
//     and creates a different one under the same name as soon as the create
//     lands. The descriptor must keep the object that was created, carry the
//     hook's open context in f_data, and not be opened a second time.
// (t) O_TRUNC is applied by the authorized open, not before it: an open the
//     filesystem refuses leaves the size unchanged.
// (l) A filesystem without the hook keeps the legacy create, truncate and
//     open order.
// (z) Argument "z": a dentry allocation that fails after the create closes
//     the open context exactly once. dentry_alloc() fails only when calloc()
//     does, which this test cannot arrange, so (z) runs only on a kernel
//     built with a test-only injection that is not in the tree, added to
//     dentry_alloc() before its calloc():
//         if (path && strstr(path, "p3-enomem")) return nullptr;
//     On a kernel without it, (z) reports FAIL. The default run skips it.
//
// After each case the fixture checks that no vnode reference, root dentry
// reference or directory lock is left behind.

#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include <osv/dentry.h>
#include <osv/file.h>
#include <osv/mount.h>
#include <osv/vnode.h>

extern "C" int namei(const char *path, struct dentry **dpp);

static int tests = 0, fails = 0;

static void report(bool ok, const std::string& msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg.c_str());
}

static bool fails_with(int r, int err)
{
    return r == -1 && errno == err;
}

// ---- fixture -------------------------------------------------------------

static const unsigned ctx_magic = 0x9f3c0de1;
struct open_ctx {
    unsigned magic = ctx_magic;
};

static struct vnops *ram;                   // ramfs vnops being wrapped
static struct vnops hook_vnops, legacy_vnops;
static struct vfsops hook_vfsops, legacy_vfsops;

static bool race, hold, deny_open;
static std::string hold_path;
static int holder_fd = -1;
static int create_err;
static int n_create_open, n_open, n_close_ctx;
static uint64_t created_ino;
static open_ctx *last_ctx;

// Leak observables of the mounted fixture. Every live vnode holds m_count
// (vget() calls vfs_busy()), every dentry holds its parent, and the root
// vnode is the directory sys_open() locks.
static struct mount *fix_mp;
static struct vnode *fix_root;
static int base_vnodes, base_root_refs;

static void reset()
{
    race = hold = deny_open = false;
    holder_fd = -1;
    create_err = 0;
    n_create_open = n_open = n_close_ctx = 0;
    created_ino = 0;
    last_ctx = nullptr;
}

// Called right after the object `name` is created in dvp. Records it and,
// if `race`, acts as an external client: renames it to "<name>.old" and
// creates a different object under `name`. If `hold`, a local open of
// hold_path then takes a dentry for whatever the name now maps to.
// Returns the created vnode (locked and referenced) in *vpp if vpp is
// non-null.
static int after_create(struct vnode *dvp, char *name, mode_t mode,
                        struct vnode **vpp)
{
    struct vnode *vp;
    int error = ram->vop_lookup(dvp, name, &vp);
    if (error) {
        return error;
    }
    created_ino = vp->v_ino;
    if (race) {
        char moved[NAME_MAX + 1];
        snprintf(moved, sizeof(moved), "%s.old", name);
        error = ram->vop_rename(dvp, vp, name, dvp, nullptr, moved);
        if (!error) {
            error = ram->vop_create(dvp, name, mode);
        }
    }
    if (!error && hold) {
        holder_fd = open(hold_path.c_str(), O_RDONLY);
        error = holder_fd < 0 ? errno : 0;
    }
    if (error || !vpp) {
        vput(vp);
        return error;
    }
    *vpp = vp;
    return 0;
}

// Legacy entry point of the hook filesystem: same server-side behaviour.
static int racing_create(struct vnode *dvp, char *name, mode_t mode)
{
    if (create_err) {
        return create_err;
    }
    int error = ram->vop_create(dvp, name, mode);
    return error ? error : after_create(dvp, name, mode, nullptr);
}

static int fix_create_open(struct vnode *dvp, const char *name, int flags,
                           mode_t mode, struct vnode **vpp, void **datap)
{
    ++n_create_open;
    if (create_err) {
        return create_err;
    }
    char n[NAME_MAX + 1];
    strlcpy(n, name, sizeof(n));
    int error = ram->vop_create(dvp, n, mode);
    if (!error) {
        error = after_create(dvp, n, mode, vpp);
    }
    if (error) {
        return error;
    }
    last_ctx = new open_ctx;
    *datap = last_ctx;
    return 0;
}

// The authorized open. The hook filesystem applies O_TRUNC itself, only
// once the open is allowed.
static int hook_open(struct file *fp)
{
    ++n_open;
    if (deny_open) {
        return EACCES;
    }
    if (fp->f_flags & O_TRUNC) {
        int error = ram->vop_truncate(file_dentry(fp)->d_vnode, 0);
        if (error) {
            return error;
        }
    }
    return ram->vop_open(fp);
}

static int legacy_open(struct file *fp)
{
    ++n_open;
    return deny_open ? EACCES : ram->vop_open(fp);
}

// A file opened by the hook carries its context; a file the VFS could not
// name (no dentry) can only have been opened by the hook. Count both, so a
// second close of the same file is visible rather than a crash in ramfs.
static int fix_close(struct vnode *vp, struct file *fp)
{
    auto c = static_cast<open_ctx *>(fp->f_data);
    if ((c && c->magic == ctx_magic) || !file_dentry(fp)) {
        ++n_close_ctx;
        fp->f_data = nullptr;
        delete c;
        return 0;   // opened by the hook, not by ramfs_open
    }
    return ram->vop_close(vp, fp);
}

static bool mount_fixture(const std::string& dir, bool hook)
{
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        report(false, "mkdir " + dir + ": " + strerror(errno));
        return false;
    }
    if (mount("none", dir.c_str(), "ramfs", 0, nullptr) != 0) {
        report(false, "mount " + dir + ": " + strerror(errno));
        return false;
    }
    struct dentry *dp;
    if (namei(dir.c_str(), &dp) != 0) {
        report(false, "namei " + dir);
        return false;
    }
    struct vnode *root = dp->d_vnode;
    struct mount *mp = dp->d_mount;
    ram = root->v_op;
    struct vnops *vn = hook ? &hook_vnops : &legacy_vnops;
    struct vfsops *ops = hook ? &hook_vfsops : &legacy_vfsops;
    *vn = *ram;
    vn->vop_open = hook ? hook_open : legacy_open;
    vn->vop_close = fix_close;
    if (hook) {
        vn->vop_create = racing_create;
        vn->vop_create_open = fix_create_open;
    }
    *ops = *mp->m_op;
    ops->vfs_vnops = vn;     // every vnode vget() makes from now on
    mp->m_op = ops;
    root->v_op = vn;
    drele(dp);
    fix_mp = mp;
    fix_root = root;
    base_vnodes = mp->m_count;
    base_root_refs = mp->m_root->d_refcnt;
    return true;
}

// With every descriptor closed, the counts are back to their values after
// mount_fixture(): no missing vput(), drele() or vn_unlock().
static void check_no_leak(const std::string& what)
{
    int v = fix_mp->m_count, d = fix_mp->m_root->d_refcnt;
    int l = fix_root->v_nrlocks;
    report(v == base_vnodes && d == base_root_refs && l == 0,
           what + ": nothing leaked (vnodes " + std::to_string(v) + "/" +
           std::to_string(base_vnodes) + ", root refs " + std::to_string(d) +
           "/" + std::to_string(base_root_refs) + ", dir locks " +
           std::to_string(l) + ")");
}

static void unmount_fixture(const std::string& dir)
{
    int r = umount(dir.c_str());
    report(r == 0, "unmount " + dir);
    rmdir(dir.c_str());
}

static off_t size_of(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? st.st_size : -1;
}

static bool put(const std::string& path, const char *data)
{
    int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        return false;
    }
    bool ok = write(fd, data, strlen(data)) == (ssize_t)strlen(data);
    return close(fd) == 0 && ok;
}

static std::string get(const std::string& path)
{
    char buf[64] = {};
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return "<open: " + std::string(strerror(errno)) + ">";
    }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    return n < 0 ? "<read error>" : std::string(buf, n);
}

// ---- cases ---------------------------------------------------------------

static void test_create_open(const std::string& dir)
{
    const std::string f = dir + "/f";
    reset();
    race = true;
    int fd = open(f.c_str(), O_CREAT | O_RDWR, 0644);
    race = false;
    if (fd < 0) {
        report(false, "(c) create: " + std::string(strerror(errno)));
        return;
    }
    struct stat st = {};
    report(n_create_open == 1, "(c) vop_create_open called once (got " +
           std::to_string(n_create_open) + ")");
    report(n_open == 0, "(c) no second open after create_open (got " +
           std::to_string(n_open) + " VOP_OPEN)");
    // Evaluate before building messages: argument order is unspecified.
    bool ok = fstat(fd, &st) == 0;
    report(ok && created_ino && st.st_ino == created_ino,
           "(c) fd keeps the created object after external replace (ino " +
           std::to_string(st.st_ino) + ", created " +
           std::to_string(created_ino) + ")");
    struct stat nst = {};
    ok = stat(f.c_str(), &nst) == 0;
    report(ok && nst.st_ino == created_ino,
           "(c) name maps to the created object while it is open (ino " +
           std::to_string(nst.st_ino) + ")");
    struct file *fp;
    if (fget(fd, &fp) == 0) {
        report(last_ctx && fp->f_data == last_ctx,
               "(c) open context is the file's f_data");
        report(!strcmp(fp->f_dentry->d_path, "/f"),
               "(c) dentry path /f (got " +
               std::string(fp->f_dentry->d_path) + ")");
        // One reference, the dentry's; the hook's lock is released.
        struct vnode *vp = fp->f_dentry->d_vnode;
        int refs = vp->v_refcnt, locks = vp->v_nrlocks;
        report(refs == 1 && locks == 0,
               "(c) created vnode: one reference, unlocked (refs " +
               std::to_string(refs) + ", locks " + std::to_string(locks) +
               ")");
        fdrop(fp);
    }
    report(write(fd, "mine", 4) == 4, "(c) write through fd");
    ok = close(fd) == 0;
    report(ok && n_close_ctx == 1,
           "(c) close releases the context once (got " +
           std::to_string(n_close_ctx) + ")");
    report(get(dir + "/f.old") == "mine",
           "(c) data landed in the created object, now named f.old");
    check_no_leak("(c) create/open");

    // A local open of the replacement caches the name for it before the
    // create returns (case B). The new fd still gets the created object,
    // and the name is cached for the created object, as with no holder:
    // the filesystem's create is the newer answer. The holder keeps its fd.
    reset();
    race = hold = true;
    hold_path = dir + "/h";
    fd = open(hold_path.c_str(), O_CREAT | O_RDWR, 0644);
    race = hold = false;
    struct stat hst = {}, pst = {};
    st = {};
    ok = fd >= 0 && holder_fd >= 0 && fstat(fd, &st) == 0 &&
         fstat(holder_fd, &hst) == 0;
    report(ok && st.st_ino == created_ino && hst.st_ino != created_ino,
           "(c) held name: fd keeps the created object (ino " +
           std::to_string(st.st_ino) + ", created " +
           std::to_string(created_ino) + ", holder " +
           std::to_string(hst.st_ino) + ")");
    ok = stat(hold_path.c_str(), &pst) == 0;
    report(ok && created_ino && pst.st_ino == created_ino,
           "(c) held name now maps to the created object, as with no holder"
           " (ino " + std::to_string(pst.st_ino) + ")");
    if (fd >= 0) {
        close(fd);
    }
    if (holder_fd >= 0) {
        close(holder_fd);
    }
    check_no_leak("(c) held name, replaced");

    // A local open of the created object itself holds its dentry: the new
    // fd shares that dentry.
    reset();
    hold = true;
    hold_path = dir + "/s";
    fd = open(hold_path.c_str(), O_CREAT | O_RDWR, 0644);
    hold = false;
    struct file *hfp;
    if (fd >= 0 && holder_fd >= 0 && fget(fd, &fp) == 0) {
        if (fget(holder_fd, &hfp) == 0) {
            report(fp->f_dentry.get() == hfp->f_dentry.get() &&
                   n_create_open == 1,
                   "(c) held name, same object: fd shares the dentry");
            fdrop(hfp);
        }
        fdrop(fp);
    } else {
        report(false, "(c) held name, same object: open");
    }
    if (fd >= 0) {
        close(fd);
    }
    if (holder_fd >= 0) {
        close(holder_fd);
    }
    check_no_leak("(c) held name, same object");

    // In a subdirectory the dentry is named below it.
    reset();
    mkdir((dir + "/sub").c_str(), 0755);
    fd = open((dir + "/sub/g").c_str(), O_CREAT | O_RDWR, 0644);
    if (fd >= 0 && fget(fd, &fp) == 0) {
        report(n_create_open == 1 && !strcmp(fp->f_dentry->d_path, "/sub/g"),
               "(c) subdirectory dentry path /sub/g (got " +
               std::string(fp->f_dentry->d_path) + ")");
        fdrop(fp);
    } else {
        report(false, "(c) subdirectory create");
    }
    if (fd >= 0) {
        close(fd);
    }
    check_no_leak("(c) subdirectory");

    // A refused create returns its error and leaves nothing to close.
    reset();
    create_err = EIO;
    fd = open((dir + "/refused").c_str(), O_CREAT | O_RDWR, 0644);
    report(fd < 0 && errno == EIO && n_close_ctx == 0,
           "(c) refused create returns EIO, nothing to close");
    if (fd >= 0) {
        close(fd);
    }
    create_err = 0;
    check_no_leak("(c) refused create");

    // Existing file: no create, ordinary open; O_EXCL still EEXIST.
    reset();
    fd = open((dir + "/f.old").c_str(), O_CREAT | O_RDONLY, 0644);
    report(fd >= 0 && n_create_open == 0 && n_open == 1,
           "(c) O_CREAT on existing file opens without create_open");
    if (fd >= 0) {
        close(fd);
    }
    report(fails_with(open((dir + "/f.old").c_str(),
                           O_CREAT | O_EXCL | O_RDWR, 0644), EEXIST) &&
           n_create_open == 0, "(c) O_EXCL on existing file is EEXIST");
    check_no_leak("(c) existing file");
}

static void test_deferred_trunc(const std::string& dir)
{
    const std::string f = dir + "/t";
    reset();
    if (!put(f, "hello") || size_of(f) != 5) {
        report(false, "(t) setup");
        return;
    }
    deny_open = true;
    report(fails_with(open(f.c_str(), O_WRONLY | O_TRUNC), EACCES),
           "(t) refused O_TRUNC open fails EACCES");
    report(size_of(f) == 5, "(t) refused O_TRUNC open leaves size (got " +
           std::to_string(size_of(f)) + ")");
    report(fails_with(open(f.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644),
                      EACCES), "(t) refused O_CREAT|O_TRUNC open fails EACCES");
    report(size_of(f) == 5,
           "(t) refused O_CREAT|O_TRUNC open leaves size (got " +
           std::to_string(size_of(f)) + ")");
    deny_open = false;
    int fd = open(f.c_str(), O_WRONLY | O_TRUNC);
    report(fd >= 0 && size_of(f) == 0, "(t) authorized O_TRUNC truncates");
    if (fd >= 0) {
        close(fd);
    }
    report(fails_with(open(f.c_str(), O_RDONLY | O_TRUNC), EINVAL),
           "(t) O_TRUNC without write access is EINVAL");
    check_no_leak("(t)");
}

static void test_legacy(const std::string& dir)
{
    const std::string f = dir + "/f";
    reset();
    int fd = open(f.c_str(), O_CREAT | O_RDWR, 0644);
    report(fd >= 0 && n_open == 1, "(l) create then VOP_OPEN once");
    report(fd >= 0 && write(fd, "hello", 5) == 5 && close(fd) == 0 &&
           size_of(f) == 5, "(l) write and close");
    report(fails_with(open(f.c_str(), O_CREAT | O_EXCL | O_RDWR, 0644),
                      EEXIST), "(l) O_EXCL on existing file is EEXIST");
    // Unchanged legacy order: the VFS truncates before VOP_OPEN.
    deny_open = true;
    report(fails_with(open(f.c_str(), O_WRONLY | O_TRUNC), EACCES) &&
           size_of(f) == 0, "(l) legacy O_TRUNC still precedes VOP_OPEN");
    deny_open = false;
    report(put(f, "again") && get(f) == "again", "(l) create/truncate/write");
    check_no_leak("(l)");
}

static void test_dentry_enomem(const std::string& dir)
{
    reset();
    int fd = open((dir + "/p3-enomem").c_str(), O_CREAT | O_RDWR, 0644);
    int err = errno;
    report(fd < 0 && err == ENOMEM, "(z) failed dentry allocation is ENOMEM");
    report(n_create_open == 1 && n_open == 0,
           "(z) created once, not opened again");
    report(n_close_ctx == 1, "(z) open context closed exactly once (got " +
           std::to_string(n_close_ctx) + ")");
    if (fd >= 0) {
        close(fd);
    }
    check_no_leak("(z)");
}

int main(int argc, char **argv)
{
    mkdir("/tmp", 0755);
    const std::string hook = "/tmp/vfs-remote-h", legacy = "/tmp/vfs-remote-l";
    if (argc > 1 && !strcmp(argv[1], "z")) {
        if (mount_fixture(hook, true)) {
            test_dentry_enomem(hook);
            unmount_fixture(hook);
        }
        printf("SUMMARY: %d tests, %d failures\n", tests, fails);
        return fails ? 1 : 0;
    }
    if (mount_fixture(hook, true)) {
        test_create_open(hook);
        test_deferred_trunc(hook);
        unmount_fixture(hook);
    }
    if (mount_fixture(legacy, false)) {
        test_legacy(legacy);
        unmount_fixture(legacy);
    }
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
