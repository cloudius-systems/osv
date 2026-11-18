/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A dataset's recordsize must bound the records its files grow.
//
// The test creates two datasets with compression off, one with
// recordsize=8K and one with recordsize=128K as a control, mounts each, and
// writes one sparse file in each: 8 KiB at offset 0 and 8 KiB at offset
// 1 MiB.  A file grows its block size up to the dataset's maximum while it
// has one block, so with 8 KiB records the file holds two 8 KiB records and
// with 128 KiB records two 128 KiB ones.  After a sync the dataset's
// referenced bytes (statfs: f_blocks - f_bfree) show which happened.
//
// The 128K control must reference at least 256 KiB more than before the
// write, which shows the measurement sees the record size.  The 8K dataset
// must reference less than 128 KiB more.
//
// Run: ./scripts/run.py -e tests/tst-zfs-recordsize-mount.so
// Requires a conf_zfs=openzfs image with a ZFS root (it uses libzfs.so).

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/statfs.h>
#include <unistd.h>

typedef struct libzfs_handle libzfs_handle_t;
typedef struct zfs_handle zfs_handle_t;
typedef struct zpool_handle zpool_handle_t;
typedef struct nvlist nvlist_t;

#define ZFS_TYPE_FILESYSTEM (1 << 0)

typedef libzfs_handle_t *(*fn_libzfs_init)(void);
typedef void (*fn_libzfs_fini)(libzfs_handle_t *);
typedef zpool_handle_t *(*fn_zpool_open)(libzfs_handle_t *, const char *);
typedef void (*fn_zpool_close)(zpool_handle_t *);
typedef int (*fn_zfs_create)(libzfs_handle_t *, const char *, int, nvlist_t *);
typedef zfs_handle_t *(*fn_zfs_open)(libzfs_handle_t *, const char *, int);
typedef int (*fn_zfs_mount)(zfs_handle_t *, const char *, int);
typedef int (*fn_zfs_unmount)(zfs_handle_t *, const char *, int);
typedef int (*fn_zfs_destroy)(zfs_handle_t *, int);
typedef void (*fn_zfs_close)(zfs_handle_t *);
typedef nvlist_t *(*fn_fnvlist_alloc)(void);
typedef void (*fn_fnvlist_free)(nvlist_t *);
typedef void (*fn_fnvlist_add_string)(nvlist_t *, const char *, const char *);

static fn_libzfs_init p_libzfs_init;
static fn_libzfs_fini p_libzfs_fini;
static fn_zpool_open p_zpool_open;
static fn_zpool_close p_zpool_close;
static fn_zfs_create p_zfs_create;
static fn_zfs_open p_zfs_open;
static fn_zfs_mount p_zfs_mount;
static fn_zfs_unmount p_zfs_unmount;
static fn_zfs_destroy p_zfs_destroy;
static fn_zfs_close p_zfs_close;
static fn_fnvlist_alloc p_fnvlist_alloc;
static fn_fnvlist_free p_fnvlist_free;
static fn_fnvlist_add_string p_fnvlist_add_string;

static const uint64_t KiB = 1024;

// Resolve the libzfs entry points; the nvlist ones live in libsolaris.so,
// which is already loaded as the file system driver.
static bool load_libzfs()
{
    void *h = dlopen("libzfs.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!h) {
        printf("FAIL: cannot load libzfs.so: %s\n", dlerror());
        return false;
    }
#define L(lib, name)                                          \
    p_##name = (fn_##name)dlsym(lib, #name);                  \
    if (!p_##name) {                                          \
        printf("FAIL: symbol " #name " not found\n");         \
        return false;                                         \
    }
    L(h, libzfs_init)
    L(h, libzfs_fini)
    L(h, zpool_open)
    L(h, zpool_close)
    L(h, zfs_create)
    L(h, zfs_open)
    L(h, zfs_mount)
    L(h, zfs_unmount)
    L(h, zfs_destroy)
    L(h, zfs_close)
    L(RTLD_DEFAULT, fnvlist_alloc)
    L(RTLD_DEFAULT, fnvlist_free)
    L(RTLD_DEFAULT, fnvlist_add_string)
#undef L
    return true;
}

static const char *detect_pool(libzfs_handle_t *zfsh)
{
    static const char *candidates[] = { "osv", "rpool", "data", nullptr };
    for (int i = 0; candidates[i]; i++) {
        zpool_handle_t *ph = p_zpool_open(zfsh, candidates[i]);
        if (ph) {
            p_zpool_close(ph);
            return candidates[i];
        }
    }
    return nullptr;
}

static void destroy_dataset(libzfs_handle_t *zfsh, const char *name)
{
    zfs_handle_t *zh = p_zfs_open(zfsh, name, ZFS_TYPE_FILESYSTEM);
    if (zh) {
        p_zfs_unmount(zh, nullptr, 0);
        p_zfs_destroy(zh, 0);
        p_zfs_close(zh);
    }
}

// Create <name> with the given recordsize and no compression, mounted at
// <mountpoint>.  The properties are set at creation, so the mount sees them.
static bool create_dataset(libzfs_handle_t *zfsh, const char *name,
                           const char *mountpoint, const char *recordsize)
{
    destroy_dataset(zfsh, name);
    nvlist_t *props = p_fnvlist_alloc();
    p_fnvlist_add_string(props, "mountpoint", mountpoint);
    p_fnvlist_add_string(props, "recordsize", recordsize);
    p_fnvlist_add_string(props, "compression", "off");
    int rc = p_zfs_create(zfsh, name, ZFS_TYPE_FILESYSTEM, props);
    p_fnvlist_free(props);
    if (rc != 0) {
        printf("FAIL: zfs_create %s rc=%d errno=%d\n", name, rc, errno);
        return false;
    }
    zfs_handle_t *zh = p_zfs_open(zfsh, name, ZFS_TYPE_FILESYSTEM);
    if (!zh) {
        printf("FAIL: zfs_open %s\n", name);
        return false;
    }
    rc = p_zfs_mount(zh, nullptr, 0);
    p_zfs_close(zh);
    if (rc != 0) {
        printf("FAIL: zfs_mount %s rc=%d errno=%d\n", name, rc, errno);
        return false;
    }
    return true;
}

// Bytes the dataset mounted at <dir> references, after a txg sync.
static bool referenced_bytes(const char *dir, uint64_t *out)
{
    sync();
    struct statfs st;
    if (statfs(dir, &st) != 0) {
        printf("FAIL: statfs %s: %s\n", dir, strerror(errno));
        return false;
    }
    *out = (uint64_t)(st.f_blocks - st.f_bfree) * st.f_bsize;
    return true;
}

// Write 8 KiB at offset 0 and at 1 MiB of a new file under <dir>, and return
// how much the dataset's referenced bytes grew.
static bool sparse_write_growth(const char *dir, uint64_t *growth)
{
    uint64_t before, after;
    if (!referenced_bytes(dir, &before)) {
        return false;
    }
    char path[256];
    snprintf(path, sizeof(path), "%s/sparse.dat", dir);
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        printf("FAIL: open %s: %s\n", path, strerror(errno));
        return false;
    }
    static char buf[8 * KiB];
    memset(buf, 0x5a, sizeof(buf));
    bool ok = pwrite(fd, buf, sizeof(buf), 0) == (ssize_t)sizeof(buf) &&
              pwrite(fd, buf, sizeof(buf), 1024 * KiB) == (ssize_t)sizeof(buf) &&
              fsync(fd) == 0;
    close(fd);
    if (!ok) {
        printf("FAIL: write %s: %s\n", path, strerror(errno));
        return false;
    }
    if (!referenced_bytes(dir, &after)) {
        return false;
    }
    *growth = after - before;
    return true;
}

int main()
{
    if (!load_libzfs()) {
        return 1;
    }
    libzfs_handle_t *zfsh = p_libzfs_init();
    if (!zfsh) {
        printf("FAIL: libzfs_init\n");
        return 1;
    }
    const char *pool = detect_pool(zfsh);
    if (!pool) {
        printf("FAIL: no ZFS pool\n");
        p_libzfs_fini(zfsh);
        return 1;
    }
    char ds8k[128], ds128k[128];
    snprintf(ds8k, sizeof(ds8k), "%s/rs-mount-8k", pool);
    snprintf(ds128k, sizeof(ds128k), "%s/rs-mount-128k", pool);

    int failures = 0;
    uint64_t g8k = 0, g128k = 0;
    if (!create_dataset(zfsh, ds128k, "/rs-mount-128k", "128K") ||
        !create_dataset(zfsh, ds8k, "/rs-mount-8k", "8K") ||
        !sparse_write_growth("/rs-mount-128k", &g128k) ||
        !sparse_write_growth("/rs-mount-8k", &g8k)) {
        failures++;
    } else {
        printf("recordsize=128K: referenced grew %llu KiB\n",
               (unsigned long long)(g128k / KiB));
        printf("recordsize=8K: referenced grew %llu KiB\n",
               (unsigned long long)(g8k / KiB));
        if (g128k < 256 * KiB) {
            printf("FAIL: the 128K control grew less than 256 KiB\n");
            failures++;
        }
        if (g8k >= 128 * KiB) {
            printf("FAIL: the 8K dataset wrote 128 KiB records\n");
            failures++;
        }
    }
    destroy_dataset(zfsh, ds8k);
    destroy_dataset(zfsh, ds128k);
    p_libzfs_fini(zfsh);
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
