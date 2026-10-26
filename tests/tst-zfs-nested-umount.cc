/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Nested ZFS datasets: unmount the child, then the parent, then export.
//
// The ZFS image builder does exactly this (scripts/upload_manifest.py):
// /zfs/zfs, then /zfs, unforced, then "zpool export". An unmount that keeps
// a reference to the dentry it covers leaves the parent mount busy, so the
// parent unmount fails with EBUSY and the export with "pool is busy".
//
// Case 1 uses child datasets of the root pool and needs nothing else.
// Case 2 creates a pool on a second disk (/dev/vblk1, e.g. run.py
// --second-disk-image) and exports it; it is skipped without that disk.

#include <sys/mount.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>

typedef struct libzfs_handle libzfs_handle_t;
typedef struct zfs_handle zfs_handle_t;
typedef struct zpool_handle zpool_handle_t;
typedef struct nvlist nvlist_t;
#define ZFS_TYPE_FILESYSTEM (1 << 0)

static libzfs_handle_t *(*p_libzfs_init)(void);
static void (*p_libzfs_fini)(libzfs_handle_t *);
static zpool_handle_t *(*p_zpool_open)(libzfs_handle_t *, const char *);
static void (*p_zpool_close)(zpool_handle_t *);
static int (*p_zpool_create)(libzfs_handle_t *, const char *, nvlist_t *,
                             nvlist_t *, nvlist_t *);
// OpenZFS takes a log string as a third argument; bsd ZFS ignores it.
static int (*p_zpool_export)(zpool_handle_t *, int, const char *);
static int (*p_zfs_create)(libzfs_handle_t *, const char *, int, nvlist_t *);
static zfs_handle_t *(*p_zfs_open)(libzfs_handle_t *, const char *, int);
static int (*p_zfs_destroy)(zfs_handle_t *, int);
static void (*p_zfs_close)(zfs_handle_t *);
static nvlist_t *(*p_fnvlist_alloc)(void);
static void (*p_fnvlist_free)(nvlist_t *);
static void (*p_fnvlist_add_string)(nvlist_t *, const char *, const char *);
static void (*p_fnvlist_add_nvlist_array)(nvlist_t *, const char *,
                                          nvlist_t **, unsigned);

static int tests = 0, fails = 0;

static void report(bool ok, const std::string& msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg.c_str());
}

static bool load_libzfs()
{
    void *h = dlopen("libzfs.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!h) {
        printf("SKIP: cannot load libzfs.so: %s\n", dlerror());
        return false;
    }
    // nvpair lives in libsolaris.so, which is already loaded.
#define L(name) \
    *(void **)&p_##name = dlsym(RTLD_DEFAULT, #name); \
    if (!p_##name) *(void **)&p_##name = dlsym(h, #name); \
    if (!p_##name) { printf("SKIP: symbol " #name " missing\n"); return false; }
    L(libzfs_init) L(libzfs_fini) L(zpool_open) L(zpool_close)
    L(zpool_create) L(zpool_export) L(zfs_create) L(zfs_open) L(zfs_destroy)
    L(zfs_close) L(fnvlist_alloc) L(fnvlist_free) L(fnvlist_add_string)
    L(fnvlist_add_nvlist_array)
#undef L
    return true;
}

static const char *root_pool(libzfs_handle_t *h)
{
    for (const char *p : {"osv", "rpool", "data"}) {
        zpool_handle_t *zp = p_zpool_open(h, p);
        if (zp) {
            p_zpool_close(zp);
            return p;
        }
    }
    return nullptr;
}

static void destroy(libzfs_handle_t *h, const std::string& ds)
{
    zfs_handle_t *zh = p_zfs_open(h, ds.c_str(), ZFS_TYPE_FILESYSTEM);
    if (zh) {
        p_zfs_destroy(zh, 0);
        p_zfs_close(zh);
    }
}

static bool create(libzfs_handle_t *h, const std::string& ds)
{
    bool ok = p_zfs_create(h, ds.c_str(), ZFS_TYPE_FILESYSTEM, nullptr) == 0;
    report(ok, "zfs_create " + ds);
    return ok;
}

// data == NULL selects "dev is the dataset name" in both ZFS ports.
static bool mount_ds(const std::string& ds, const std::string& dir)
{
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        report(false, "mkdir " + dir + ": " + strerror(errno));
        return false;
    }
    bool ok = mount(ds.c_str(), dir.c_str(), "zfs", 0, nullptr) == 0;
    report(ok, "mount " + ds + " at " + dir +
           (ok ? "" : std::string(": ") + strerror(errno)));
    return ok;
}

static bool touch(const std::string& path)
{
    int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    bool ok = fd >= 0 && write(fd, "x", 1) == 1;
    if (fd >= 0) {
        ok = close(fd) == 0 && ok;
    }
    report(ok, "write " + path);
    return ok;
}

static bool unmount(const std::string& dir, const std::string& what)
{
    int r = umount(dir.c_str());
    int err = errno;
    report(r == 0, what + ": umount " + dir + " returns 0" +
           (r == 0 ? "" : std::string(" (got ") + strerror(err) + ")"));
    return r == 0;
}

// Case 1: osv/<p> and osv/<p>/c on the root pool.
static void root_pool_case(libzfs_handle_t *h, const char *pool)
{
    const std::string p = std::string(pool) + "/nested-umount";
    const std::string c = p + "/c", pdir = "/nested-umount";
    const std::string cdir = pdir + "/c";
    destroy(h, c);
    destroy(h, p);
    if (!create(h, p) || !create(h, c) || !mount_ds(p, pdir) ||
        !mount_ds(c, cdir) || !touch(cdir + "/f")) {
        return;
    }
    if (!unmount(cdir, "(1) child") || !unmount(pdir, "(1) parent")) {
        return;
    }
    zfs_handle_t *zh = p_zfs_open(h, c.c_str(), ZFS_TYPE_FILESYSTEM);
    report(zh && p_zfs_destroy(zh, 0) == 0, "(1) destroy " + c);
    if (zh) {
        p_zfs_close(zh);
    }
    zh = p_zfs_open(h, p.c_str(), ZFS_TYPE_FILESYSTEM);
    report(zh && p_zfs_destroy(zh, 0) == 0, "(1) destroy " + p);
    if (zh) {
        p_zfs_close(zh);
    }
    rmdir(pdir.c_str());
}

// Case 2: a pool on /dev/vblk1 with its root and one child mounted, as the
// image builder has them, then unmounted and exported.
static void export_case(libzfs_handle_t *h)
{
    struct stat st;
    if (stat("/dev/vblk1", &st) != 0) {
        printf("SKIP: (2) export case needs a second disk (/dev/vblk1)\n");
        return;
    }
    const std::string pool = "nestedumount", c = pool + "/c";
    const std::string pdir = "/" + pool, cdir = pdir + "/c";
    nvlist_t *disk = p_fnvlist_alloc();
    p_fnvlist_add_string(disk, "type", "disk");
    p_fnvlist_add_string(disk, "path", "/dev/vblk1");
    nvlist_t *root = p_fnvlist_alloc();
    p_fnvlist_add_string(root, "type", "root");
    p_fnvlist_add_nvlist_array(root, "children", &disk, 1);
    // OSv cannot write zpool.cache.
    nvlist_t *props = p_fnvlist_alloc();
    p_fnvlist_add_string(props, "cachefile", "none");
    bool ok = p_zpool_create(h, pool.c_str(), root, props, nullptr) == 0;
    p_fnvlist_free(props);
    p_fnvlist_free(root);
    p_fnvlist_free(disk);
    report(ok, "(2) zpool_create " + pool + " on /dev/vblk1");
    if (!ok || !create(h, c) || !mount_ds(pool, pdir) ||
        !mount_ds(c, cdir) || !touch(cdir + "/f")) {
        return;
    }
    if (!unmount(cdir, "(2) child")) {
        return;
    }
    // Export even if the parent unmount failed, to show what it does then.
    unmount(pdir, "(2) parent");
    zpool_handle_t *zp = p_zpool_open(h, pool.c_str());
    int r = zp ? p_zpool_export(zp, 0, nullptr) : -1;
    report(r == 0, "(2) zpool export " + pool + " returns 0");
    if (zp) {
        p_zpool_close(zp);
    }
}

int main()
{
    if (!load_libzfs()) {
        return 0;
    }
    libzfs_handle_t *h = p_libzfs_init();
    if (!h) {
        printf("SKIP: libzfs_init failed\n");
        return 0;
    }
    const char *pool = root_pool(h);
    if (!pool) {
        printf("SKIP: no ZFS root pool\n");
        p_libzfs_fini(h);
        return 0;
    }
    root_pool_case(h, pool);
    export_case(h);
    p_libzfs_fini(h);
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
