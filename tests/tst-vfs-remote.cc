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
// A third mount opts into MNT_NONAMECACHE (every lookup asks the
// filesystem) and adds vop_access. The fixture sets the flag right after
// mount(), before any lookup below the mount, as a filesystem's vfs_mount
// would. An external client replaces objects behind the name cache by
// calling ramfs directly while the VFS still holds their old dentries.
// (w) A held file name: a whole-path lookup sees the replacement, the held
//     fd keeps the old object, an unchanged held name shares its dentry.
// (d) A held directory: a path below it sees the new directory's children,
//     both for a cached full path and for a component walk.
// (n) A held symlink: lstat and O_NOFOLLOW see the file that replaced it.
// (g) The mount point and a trailing slash still resolve.
// (a) vop_access decides access in both directions; a read-only mount
//     rejects write before asking it.
// The legacy mount runs (w), (d) and (n) as controls: its cache still
// answers held names, and MNT_NONAMECACHE passed to mount(2) is ignored.
//
// Metadata the filesystem is authoritative for. The fixture's getattr can
// report a size and an AT_NBLOCKS block count that differ from the
// vnode's cached v_size, as a server can.
// (e) After the file grows behind the VFS, SEEK_END on the no-name-cache
//     mount refreshes the size, and a failed refresh fails the seek; the
//     legacy mount keeps the cached size. On every mount, an offset that
//     overflows off_t is EINVAL, as on Linux, and a bad whence leaves no
//     lock held.
// (b) stat reports the filesystem's blocks when AT_NBLOCKS is valid, and
//     derives them from the size when it is not.
// (m) A chmod or fchmod the filesystem refuses leaves the mode vn_access()
//     checks unchanged.
//
// After each case the fixture checks that no vnode reference, root dentry
// reference or directory lock is left behind.

#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/uio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include <osv/dentry.h>
#include <osv/file.h>
#include <osv/mount.h>
#include <osv/vnode.h>
#include <osv/vnode_attr.h>

extern "C" int namei(const char *path, struct dentry **dpp);

extern "C" int namei_last_nofollow(char *path, struct dentry *ddp,
                                   struct dentry **dpp);
extern "C" struct dentry *dentry_lookup(struct mount *mp, char *path);

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
static struct vnops hook_vnops, legacy_vnops, nc_vnops;
static struct vfsops hook_vfsops, legacy_vfsops, nc_vfsops;

static bool race, hold, deny_open;
static std::string hold_path;
static int holder_fd = -1;
static int create_err;
static int n_create_open, n_open, n_close_ctx;
static uint64_t created_ino;
static open_ctx *last_ctx;
static int n_lookup, n_access, access_err;
static int getattr_err, setattr_err;
static bool sized, blocks_set;         // getattr overrides, for one inode
static uint64_t sized_ino, blocks_ino, server_blocks;
static off_t server_size;

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
    n_lookup = n_access = access_err = 0;
    getattr_err = setattr_err = 0;
    sized = blocks_set = false;
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

static int fix_lookup(struct vnode *dvp, char *name, struct vnode **vpp)
{
    ++n_lookup;
    return ram->vop_lookup(dvp, name, vpp);
}

static int fix_access(struct vnode *vp, int flags)
{
    ++n_access;
    return access_err;
}

// The filesystem's answer, which may differ from the vnode's cache.
static int fix_getattr(struct vnode *vp, struct vattr *va)
{
    if (getattr_err) {
        return getattr_err;
    }
    int error = ram->vop_getattr(vp, va);
    if (!error && sized && vp->v_ino == sized_ino) {
        va->va_size = server_size;
    }
    if (!error && blocks_set && vp->v_ino == blocks_ino) {
        va->va_mask |= AT_NBLOCKS;
        va->va_nblocks = server_blocks;
    }
    return error;
}

static int fix_setattr(struct vnode *vp, struct vattr *va)
{
    return setattr_err ? setattr_err : ram->vop_setattr(vp, va);
}

// nocache: the filesystem opts into MNT_NONAMECACHE and adds vop_access.
// mflags are passed to mount(2).
static bool mount_fixture(const std::string& dir, bool hook,
                          bool nocache = false, unsigned long mflags = 0)
{
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        report(false, "mkdir " + dir + ": " + strerror(errno));
        return false;
    }
    if (mount("none", dir.c_str(), "ramfs", mflags, nullptr) != 0) {
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
    struct vnops *vn = nocache ? &nc_vnops : hook ? &hook_vnops : &legacy_vnops;
    struct vfsops *ops = nocache ? &nc_vfsops : hook ? &hook_vfsops :
                         &legacy_vfsops;
    *vn = *ram;
    vn->vop_open = hook ? hook_open : legacy_open;
    vn->vop_close = fix_close;
    vn->vop_lookup = fix_lookup;
    vn->vop_getattr = fix_getattr;
    vn->vop_setattr = fix_setattr;
    if (hook) {
        vn->vop_create = racing_create;
        vn->vop_create_open = fix_create_open;
    }
    if (nocache) {
        vn->vop_access = fix_access;
        mp->m_flags |= MNT_NONAMECACHE;
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
// mount_fixture(): no missing vput(), drele(), vn_unlock() or
// vfs_putroot().
static void check_no_leak(const std::string& what)
{
    int v = fix_mp->m_count, d = fix_mp->m_root->d_refcnt;
    int l = fix_root->v_nrlocks, p = fix_mp->m_lookups;
    report(v == base_vnodes && d == base_root_refs && l == 0 && p == 0,
           what + ": nothing leaked (vnodes " + std::to_string(v) + "/" +
           std::to_string(base_vnodes) + ", root refs " + std::to_string(d) +
           "/" + std::to_string(base_root_refs) + ", dir locks " +
           std::to_string(l) + ", pins " + std::to_string(p) + ")");
}

// Whether the name cache maps `path` (mount-relative) to fd's dentry. A
// dentry is hashed only while referenced, so a replaced dentry that stayed
// hashed is found here once the lookup that replaced it is done.
static bool cache_is(const char *path, int fd)
{
    char p[PATH_MAX];
    strlcpy(p, path, sizeof(p));
    struct dentry *dp = dentry_lookup(fix_mp, p);
    bool is = false;
    struct file *fp;
    if (dp && fget(fd, &fp) == 0) {
        is = dp == fp->f_dentry.get();
        fdrop(fp);
    }
    if (dp) {
        drele(dp);
    }
    return is;
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

// ---- P4: names asked of the filesystem ------------------------------------

static ino_t ino_of(int fd)
{
    struct stat st = {};
    return fstat(fd, &st) == 0 ? st.st_ino : (ino_t)-1;
}

static ino_t ino_at(const std::string& path)
{
    struct stat st = {};
    return stat(path.c_str(), &st) == 0 ? st.st_ino : (ino_t)-1;
}

// Locked, referenced vnode of `name` in the mount root, from ramfs itself.
static int ext_vnode(const char *name, struct vnode **vpp)
{
    char n[NAME_MAX + 1];
    strlcpy(n, name, sizeof(n));
    return ram->vop_lookup(fix_root, n, vpp);
}

// An external client, behind the VFS name cache: renames `name` in the
// mount root to "<name>.old", then, unless type is VNON, makes a new
// object of that type under `name` and returns its inode in *ino.
static int ext_replace(const char *name, int type, uint64_t *ino = nullptr)
{
    char n[NAME_MAX + 1], moved[NAME_MAX + 1];
    strlcpy(n, name, sizeof(n));
    snprintf(moved, sizeof(moved), "%s.old", name);
    struct vnode *vp;
    int error = ext_vnode(n, &vp);
    if (error) {
        return error;
    }
    error = ram->vop_rename(fix_root, vp, n, fix_root, nullptr, moved);
    vput(vp);
    if (error || type == VNON) {
        return error;
    }
    error = type == VDIR ? ram->vop_mkdir(fix_root, n, S_IFDIR | 0755) :
                           ram->vop_create(fix_root, n, S_IFREG | 0644);
    if (!error && ino && !(error = ext_vnode(n, &vp))) {
        *ino = vp->v_ino;
        vput(vp);
    }
    return error;
}

// External create of `child` in the directory `dir` of the mount root.
static int ext_create(const char *dir, const char *child, uint64_t *ino)
{
    struct vnode *dvp, *vp;
    char c[NAME_MAX + 1];
    strlcpy(c, child, sizeof(c));
    int error = ext_vnode(dir, &dvp);
    if (error) {
        return error;
    }
    error = ram->vop_create(dvp, c, S_IFREG | 0644);
    if (!error && !(error = ram->vop_lookup(dvp, c, &vp))) {
        *ino = vp->v_ino;
        vput(vp);
    }
    vput(dvp);
    return error;
}

static std::string ino_msg(ino_t got, uint64_t want)
{
    return " (ino " + std::to_string(got) + ", want " + std::to_string(want) +
           ")";
}

// nc: the mount sets MNT_NONAMECACHE; otherwise the legacy controls, where
// the cache answers for every held name.
static void test_names(const std::string& dir, bool nc)
{
    const std::string tag = nc ? "(w) " : "(l) (w) ";
    const std::string w = dir + "/w", u = dir + "/u";
    reset();
    int wfd = -1, ufd = -1;
    uint64_t fresh = 0;
    if (!put(w, "old") || !put(u, "same") ||
        (wfd = open(w.c_str(), O_RDONLY)) < 0 ||
        (ufd = open(u.c_str(), O_RDONLY)) < 0) {
        report(false, tag + "setup");
        return;
    }
    ino_t held = ino_of(wfd);
    report(ext_replace("w", VREG, &fresh) == 0, tag + "external replace");
    ino_t now = ino_at(w);
    if (nc) {
        report(now == fresh, tag + "whole path sees the replacement" +
               ino_msg(now, fresh));
    } else {
        report(now == held, tag + "cache answers the held name" +
               ino_msg(now, held));
    }
    char buf[8] = {};
    bool ok = pread(wfd, buf, sizeof(buf), 0) == 3 && !strcmp(buf, "old");
    ino_t fino = ino_of(wfd);
    report(ok && fino == held, tag + "held fd keeps the old object" +
           ino_msg(fino, held));
    report(cache_is("/w", wfd) == !nc, tag + (nc ?
           "replaced dentry is unhashed, its holder keeps it" :
           "held dentry is still cached"));
    report(get(w) == (nc ? "" : "old"), tag + "open by name reads " +
           (nc ? "the replacement" : "the cached object"));

    // An unchanged held name: asked of the filesystem (nc), same dentry.
    struct file *fp;
    struct dentry *dp = nullptr;
    n_lookup = 0;
    if (fget(ufd, &fp) == 0) {
        bool found = namei(u.c_str(), &dp) == 0;
        report(found && dp == fp->f_dentry.get() &&
               (nc ? n_lookup > 0 : n_lookup == 0),
               tag + "unchanged held name shares its dentry (lookups " +
               std::to_string(n_lookup) + ")");
        if (found) {
            drele(dp);
        }
        fdrop(fp);
    }
    // Removed behind the cache.
    report(ext_replace("u", VNON) == 0, tag + "external remove");
    ok = ino_at(u) == (ino_t)-1 && errno == ENOENT;
    report(nc ? ok : !ok, tag + (nc ? "removed held name is ENOENT" :
                                 "removed held name still cached"));

    close(wfd);
    close(ufd);
    check_no_leak(tag + "whole path");

    // A held directory replaced: its children are the new directory's.
    const std::string t2 = nc ? "(d) " : "(l) (d) ";
    reset();
    int dfd = -1, yfd = -1;
    uint64_t newd = 0, newx = 0, newy = 0;
    ino_t oldy;
    if (mkdir((dir + "/d").c_str(), 0755) != 0 || !put(dir + "/d/y", "y") ||
        (dfd = open((dir + "/d").c_str(), O_RDONLY)) < 0 ||
        (yfd = open((dir + "/d/y").c_str(), O_RDONLY)) < 0) {
        report(false, t2 + "setup");
        return;
    }
    oldy = ino_of(yfd);
    ok = ext_replace("d", VDIR, &newd) == 0 &&
         ext_create("d", "x", &newx) == 0 && ext_create("d", "y", &newy) == 0;
    report(ok, t2 + "external directory replace");
    now = ino_at(dir + "/d/x");
    if (nc) {
        report(now == newx, t2 + "component walk finds the new child" +
               ino_msg(now, newx));
    } else {
        report(now == (ino_t)-1, t2 + "walk through the cached directory"
               " misses the new child");
    }
    now = ino_at(dir + "/d/y");
    report(now == (nc ? newy : oldy), t2 + "cached full path " +
           (nc ? "is replaced" : "still answers") + ino_msg(now,
           nc ? newy : oldy));
    now = ino_at(dir + "/d");
    fino = ino_of(dfd);
    report(nc ? now == newd && fino != newd : now == fino,
           t2 + "directory name" + ino_msg(now, nc ? newd : fino));
    report(ino_of(yfd) == oldy, t2 + "held child fd keeps its object");
    close(yfd);
    close(dfd);
    check_no_leak(t2 + "directory");

    // A held symlink replaced by a file: lstat and O_NOFOLLOW see the file.
    const std::string t3 = nc ? "(n) " : "(l) (n) ";
    reset();
    const std::string n = dir + "/n";
    struct dentry *ddp = nullptr, *ndp = nullptr;
    char np[PATH_MAX];
    strlcpy(np, n.c_str(), sizeof(np));
    if (!put(dir + "/t", "t") || symlink("t", n.c_str()) != 0 ||
        namei(dir.c_str(), &ddp) != 0) {
        report(false, t3 + "setup");
        return;
    }
    ok = namei_last_nofollow(np, ddp, &ndp) == 0 &&
         ndp->d_vnode->v_type == VLNK;
    drele(ddp);
    report(ok, t3 + "symlink dentry held");
    report(ext_replace("n", VREG, &fresh) == 0, t3 + "external replace");
    struct stat st = {};
    ok = lstat(n.c_str(), &st) == 0;
    if (nc) {
        report(ok && S_ISREG(st.st_mode) && st.st_ino == fresh,
               t3 + "lstat sees the file" + ino_msg(st.st_ino, fresh));
    } else {
        report(ok && S_ISLNK(st.st_mode), t3 + "lstat sees the cached link");
    }
    int fd = open(n.c_str(), O_RDONLY | O_NOFOLLOW);
    int err = errno;
    report(nc ? fd >= 0 : fd < 0 && err == ELOOP, t3 + "O_NOFOLLOW " +
           (nc ? "opens the file" : "still ELOOP"));
    if (fd >= 0) {
        close(fd);
    }
    if (ndp) {
        drele(ndp);
    }
    check_no_leak(t3 + "nofollow");
}

// The mount point resolves to the mount's root, also through lstat.
static void test_mount_root(const std::string& dir)
{
    reset();
    struct stat st = {}, lst = {};
    bool ok = lstat(dir.c_str(), &lst) == 0;
    report(ok && S_ISDIR(lst.st_mode) && lst.st_ino == fix_root->v_ino,
           "(g) lstat of the mount point is its root" +
           ino_msg(lst.st_ino, fix_root->v_ino));
    ok = stat((dir + "/").c_str(), &st) == 0;
    report(ok && st.st_ino == lst.st_ino && st.st_dev == lst.st_dev,
           "(g) stat of the mount point is its root");
    // test_names() left a directory "d"; a trailing slash names it.
    ok = lstat((dir + "/d/").c_str(), &lst) == 0;
    report(ok && S_ISDIR(lst.st_mode) && lst.st_ino == ino_at(dir + "/d"),
           "(g) lstat with a trailing slash is the directory");
    check_no_leak("(g)");
}

// vop_access decides; a read-only mount refuses write before asking. The
// file stays open so its vnode, and the mode chmod() gave it, stay cached
// (ramfs_lookup() gives a fresh vnode ALLPERMS).
static void test_access(const std::string& dir, bool nc)
{
    const std::string tag = nc ? "(a) " : "(l) (a) ";
    const std::string f = dir + "/acc";
    reset();
    int fd = -1;
    if (!put(f, "a") || (fd = open(f.c_str(), O_RDONLY)) < 0) {
        report(false, tag + "setup");
        return;
    }
    if (nc) {
        access_err = EACCES;
        report(fails_with(access(f.c_str(), R_OK), EACCES) && n_access > 0,
               tag + "hook refuses read the mode allows");
        int wfd = open(f.c_str(), O_WRONLY);
        report(wfd < 0 && errno == EACCES, tag + "hook refuses open for write");
        if (wfd >= 0) {
            close(wfd);
        }
        access_err = 0;
    }
    bool ok = chmod(f.c_str(), 0) == 0;
    if (nc) {
        report(ok && access(f.c_str(), R_OK) == 0,
               tag + "hook allows read the mode refuses");
        fix_mp->m_flags |= MNT_RDONLY;
        n_access = 0;
        report(fails_with(access(f.c_str(), W_OK), EROFS) && n_access == 0,
               tag + "read-only mount: EROFS before the hook (calls " +
               std::to_string(n_access) + ")");
        n_access = 0;
        report(access(f.c_str(), R_OK) == 0 && n_access == 1,
               tag + "read-only mount: read still asks the hook");
        fix_mp->m_flags &= ~MNT_RDONLY;
    } else {
        report(ok && fails_with(access(f.c_str(), R_OK), EACCES),
               tag + "mode 0 is EACCES without a hook");
    }
    close(fd);
    check_no_leak(tag);
}

// ---- P4: metadata the filesystem is authoritative for ---------------------

// Another client appends `data` to the file open as fd: the bytes land and
// the filesystem reports the new size, but the vnode's cached size stays.
static bool ext_grow(int fd, const char *data)
{
    struct file *fp;
    if (fget(fd, &fp) != 0) {
        return false;
    }
    struct vnode *vp = fp->f_dentry->d_vnode;
    vn_lock(vp);
    off_t old = vp->v_size;
    struct iovec iov = {(void *)data, strlen(data)};
    struct uio uio = {&iov, 1, old, (ssize_t)strlen(data), UIO_WRITE};
    int error = ram->vop_write(vp, &uio, 0);
    sized = true;
    sized_ino = vp->v_ino;
    server_size = old + strlen(data);
    vp->v_size = old;
    vn_unlock(vp);
    fdrop(fp);
    return !error;
}

static int file_locks(int fd)
{
    struct file *fp;
    if (fget(fd, &fp) != 0) {
        return -1;
    }
    int locks = fp->f_dentry->d_vnode->v_nrlocks;
    fdrop(fp);
    return locks;
}

static void test_seek_end(const std::string& dir, bool nc)
{
    const std::string tag = nc ? "(e) " : "(l) (e) ";
    const std::string f = dir + "/e";
    reset();
    int fd = -1;
    if (!put(f, "abc") || (fd = open(f.c_str(), O_RDWR)) < 0 ||
        !ext_grow(fd, "defgh")) {
        report(false, tag + "setup");
        if (fd >= 0) {
            close(fd);
        }
        return;
    }
    off_t end = lseek(fd, 0, SEEK_END);
    if (nc) {
        report(end == 8, tag + "SEEK_END sees the size the filesystem"
               " reports (got " + std::to_string(end) + ")");
        char buf[8] = {};
        report(pread(fd, buf, sizeof(buf) - 1, 3) == 5 &&
               !strcmp(buf, "defgh"), tag + "the added bytes are readable");
        end = lseek(fd, -2, SEEK_END);
        report(end == 6, tag + "SEEK_END - 2 (got " + std::to_string(end) +
               ")");
        getattr_err = EIO;
        bool failed = fails_with(lseek(fd, 0, SEEK_END), EIO);
        getattr_err = 0;
        end = lseek(fd, 0, SEEK_CUR);
        report(failed && end == 6, tag + "failed refresh is EIO, offset"
               " unchanged (at " + std::to_string(end) + ")");
        // (S3) A server size the VFS cannot trust: a negative va_size must
        // be rejected, not fed to the SEEK_END arithmetic and on to mmap.
        off_t saved = server_size;
        server_size = -1;
        bool neg = fails_with(lseek(fd, 0, SEEK_END), EIO);
        server_size = saved;
        end = lseek(fd, 0, SEEK_CUR);
        report(neg && end == 6, tag + "a negative server size is EIO,"
               " offset unchanged (at " + std::to_string(end) + ")");
    } else {
        report(end == 3, tag + "SEEK_END uses the cached size (got " +
               std::to_string(end) + ")");
    }
    report(fails_with(lseek(fd, LLONG_MAX, SEEK_END), EINVAL),
           tag + "SEEK_END past the largest off_t is EINVAL");
    report(lseek(fd, 1, SEEK_SET) == 1 &&
           fails_with(lseek(fd, LLONG_MAX, SEEK_CUR), EINVAL) &&
           lseek(fd, 0, SEEK_CUR) == 1,
           tag + "SEEK_CUR past the largest off_t is EINVAL, offset"
           " unchanged");
    report(fails_with(lseek(fd, 0, 99), EINVAL), tag + "bad whence is EINVAL");
    int locks = file_locks(fd);
    report(locks == 0, tag + "lseek leaves the file unlocked (locks " +
           std::to_string(locks) + ")");
    close(fd);
    check_no_leak(tag);
}

static void test_blocks(const std::string& dir)
{
    const std::string f = dir + "/s", g = dir + "/s2";
    const off_t size = 1 << 20;
    reset();
    int fd = open(f.c_str(), O_CREAT | O_RDWR, 0644);
    int gd = open(g.c_str(), O_CREAT | O_RDWR, 0644);
    struct file *fp;
    if (fd < 0 || gd < 0 || ftruncate(fd, size) != 0 ||
        ftruncate(gd, size) != 0 || fget(fd, &fp) != 0) {
        report(false, "(b) setup");
    } else {
        blocks_ino = fp->f_dentry->d_vnode->v_ino;
        fdrop(fp);
        blocks_set = true;
        server_blocks = 8;
        struct stat st = {};
        bool ok = stat(f.c_str(), &st) == 0;
        report(ok && st.st_blocks == 8 && st.st_size == size,
               "(b) stat reports the filesystem's blocks (got " +
               std::to_string(st.st_blocks) + ")");
        server_blocks = 0;
        st = {};
        ok = fstat(fd, &st) == 0;
        report(ok && st.st_blocks == 0, "(b) fstat of an all-hole file: 0"
               " blocks (got " + std::to_string(st.st_blocks) + ")");
        st = {};
        ok = stat(g.c_str(), &st) == 0;
        report(ok && st.st_blocks == size / 512, "(b) without AT_NBLOCKS,"
               " blocks derive from the size (got " +
               std::to_string(st.st_blocks) + ")");
    }
    if (fd >= 0) {
        close(fd);
    }
    if (gd >= 0) {
        close(gd);
    }
    check_no_leak("(b)");
}

// On the legacy mount vn_access() checks the cached v_mode. The file stays
// open so that vnode stays cached.
static void test_chmod_failure(const std::string& dir)
{
    const std::string f = dir + "/m";
    reset();
    int fd = -1;
    if (!put(f, "m") || (fd = open(f.c_str(), O_RDONLY)) < 0 ||
        chmod(f.c_str(), 0644) != 0) {
        report(false, "(m) setup");
        if (fd >= 0) {
            close(fd);
        }
        return;
    }
    setattr_err = EPERM;
    bool failed = fails_with(chmod(f.c_str(), 0), EPERM);
    report(failed && access(f.c_str(), R_OK) == 0,
           "(m) refused chmod: read is still allowed");
    struct file *fp;
    if (fget(fd, &fp) == 0) {
        mode_t m = fp->f_dentry->d_vnode->v_mode;
        report((m & 07777) == 0644, "(m) refused chmod: v_mode unchanged"
               " (got " + std::to_string(m & 07777) + ")");
        fdrop(fp);
    }
    failed = fails_with(fchmod(fd, 0), EPERM);
    report(failed && access(f.c_str(), W_OK) == 0,
           "(m) refused fchmod: write is still allowed");
    setattr_err = 0;
    struct stat st = {};
    report(chmod(f.c_str(), 0) == 0 && stat(f.c_str(), &st) == 0 &&
           (st.st_mode & 07777) == 0 &&
           fails_with(access(f.c_str(), R_OK), EACCES),
           "(m) successful chmod publishes the mode");
    close(fd);
    check_no_leak("(m)");
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
    const std::string nc = "/tmp/vfs-remote-n";
    if (mount_fixture(nc, false, true)) {
        test_names(nc, true);
        test_access(nc, true);
        test_mount_root(nc);
        test_seek_end(nc, true);
        test_blocks(nc);
        unmount_fixture(nc);
    }
    // MNT_NONAMECACHE is for filesystems to set, not a mount(2) flag.
    if (mount_fixture(legacy, false, false, MNT_NONAMECACHE)) {
        report(!(fix_mp->m_flags & MNT_NONAMECACHE),
               "(l) MNT_NONAMECACHE from mount(2) is ignored");
        test_legacy(legacy);
        test_names(legacy, false);
        test_access(legacy, false);
        test_seek_end(legacy, false);
        test_chmod_failure(legacy);
        unmount_fixture(legacy);
    }
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
