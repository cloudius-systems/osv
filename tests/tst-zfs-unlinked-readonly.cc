/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Check that mounting a read-only ZFS dataset leaves its unlinked set alone.
//
// A file is written, fsync'd and unlinked while still open, and a snapshot
// is taken, so the snapshot's unlinked set holds the file.  Each clone of
// that snapshot starts with the same entry.  Two clones are mounted
// read-only, one through the readonly property and one with MS_RDONLY, and
// the mount must not drain the entry: the clone's "written" and
// "referenced" must not change.  Each clone is then mounted read-write, and
// that mount must drain the entry and give the file's space back, which
// shows the entry was there to drain.
//
// It drives the OpenZFS userspace API through dlopen("libzfs.so"), so it
// only runs in a conf_zfs=openzfs image.

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

typedef struct libzfs_handle libzfs_handle_t;
typedef struct zfs_handle zfs_handle_t;
typedef struct zpool_handle zpool_handle_t;

#define ZFS_TYPE_FILESYSTEM (1 << 0)
#define ZFS_TYPE_SNAPSHOT (1 << 1)

static libzfs_handle_t* (*p_libzfs_init)(void);
static void (*p_libzfs_fini)(libzfs_handle_t*);
static zpool_handle_t* (*p_zpool_open)(libzfs_handle_t*, const char*);
static void (*p_zpool_close)(zpool_handle_t*);
static int (*p_zfs_create)(libzfs_handle_t*, const char*, int, void*);
static zfs_handle_t* (*p_zfs_open)(libzfs_handle_t*, const char*, int);
static void (*p_zfs_close)(zfs_handle_t*);
static int (*p_zfs_destroy)(zfs_handle_t*, int);
static int (*p_zfs_snapshot)(libzfs_handle_t*, const char*, int, void*);
static int (*p_zfs_clone)(zfs_handle_t*, const char*, void*);
static int (*p_zfs_prop_set)(zfs_handle_t*, const char*, const char*);
static uint64_t (*p_zfs_prop_get_int)(zfs_handle_t*, int);
static int (*p_zfs_name_to_prop)(const char*);

static const long file_mib = 32;
static libzfs_handle_t* hdl;
static std::string pool;
static int failures;

static bool load_libzfs()
{
    void* h = dlopen("libzfs.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!h) {
        printf("SKIP: cannot load libzfs.so: %s\n", dlerror());
        return false;
    }
    // A symbol libzfs.so takes from libsolaris.so (the resident fs driver)
    // is looked up globally, as tst-zfs-encryption does for nvlist.
#define L(name)                                                     \
    *(void**)&p_##name = dlsym(h, #name);                           \
    if (!p_##name) {                                                \
        *(void**)&p_##name = dlsym(RTLD_DEFAULT, #name);            \
    }                                                               \
    if (!p_##name) {                                                \
        printf("SKIP: symbol " #name " not found\n");               \
        return false;                                               \
    }
    L(libzfs_init) L(libzfs_fini) L(zpool_open) L(zpool_close)
    L(zfs_create) L(zfs_open) L(zfs_close) L(zfs_destroy)
    L(zfs_snapshot) L(zfs_clone) L(zfs_prop_set) L(zfs_prop_get_int)
    L(zfs_name_to_prop)
#undef L
    return true;
}

static void check(bool ok, const std::string& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) {
        failures++;
    }
}

// Read a numeric property through a fresh handle, so that libzfs does not
// answer from the properties it cached when an earlier handle was opened.
static uint64_t prop(const std::string& ds, const char* name)
{
    sync();
    zfs_handle_t* zh = p_zfs_open(hdl, ds.c_str(), ZFS_TYPE_FILESYSTEM);
    if (!zh) {
        return UINT64_MAX;
    }
    uint64_t v = p_zfs_prop_get_int(zh, p_zfs_name_to_prop(name));
    p_zfs_close(zh);
    return v;
}

static void destroy(const std::string& name, int type)
{
    zfs_handle_t* zh = p_zfs_open(hdl, name.c_str(), type);
    if (zh) {
        p_zfs_destroy(zh, 0);
        p_zfs_close(zh);
    }
}

// Write file_mib MiB of incompressible data, fsync it, unlink it while it is
// open, and snapshot the dataset, so the snapshot's unlinked set holds it.
static bool make_snapshot(const std::string& ds, const std::string& snap)
{
    std::string mnt = "/" + ds.substr(pool.size() + 1);
    mkdir(mnt.c_str(), 0755);
    if (p_zfs_create(hdl, ds.c_str(), ZFS_TYPE_FILESYSTEM, nullptr) != 0 ||
        (mount(ds.c_str(), mnt.c_str(), "zfs", 0, "") != 0 && errno != EBUSY)) {
        perror("create or mount the source dataset");
        return false;
    }
    std::string path = mnt + "/f";
    int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    static uint64_t buf[1 << 17];
    uint64_t x = 0x9e3779b97f4a7c15ULL;
    for (long i = 0; fd >= 0 && i < file_mib; i++) {
        for (auto& w : buf) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            w = x;
        }
        if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
            perror("write");
            return false;
        }
    }
    if (fd < 0 || fsync(fd) != 0 || unlink(path.c_str()) != 0) {
        perror(path.c_str());
        return false;
    }
    sync();
    int rc = p_zfs_snapshot(hdl, snap.c_str(), 0, nullptr);
    close(fd);
    umount(mnt.c_str());
    return rc == 0;
}

// Clone the snapshot, mount the clone read-only (by the readonly property
// when by_prop, else with MS_RDONLY), and check that the mount wrote
// nothing.  Then mount it read-write and check that the file's space is
// freed.
static void run_case(const std::string& snap, const std::string& clone, bool by_prop)
{
    std::string how = by_prop ? "readonly=on" : "MS_RDONLY";
    std::string mnt = "/" + clone.substr(pool.size() + 1);
    zfs_handle_t* sh = p_zfs_open(hdl, snap.c_str(), ZFS_TYPE_SNAPSHOT);
    bool ok = sh && p_zfs_clone(sh, clone.c_str(), nullptr) == 0;
    if (sh) {
        p_zfs_close(sh);
    }
    zfs_handle_t* ch = ok ? p_zfs_open(hdl, clone.c_str(), ZFS_TYPE_FILESYSTEM) : nullptr;
    if (ch && by_prop) {
        ok = p_zfs_prop_set(ch, "readonly", "on") == 0;
    }
    if (ch) {
        p_zfs_close(ch);
    }
    mkdir(mnt.c_str(), 0755);
    check(ok, how + ": clone " + clone);
    if (!ok) {
        return;
    }

    uint64_t w0 = prop(clone, "written"), r0 = prop(clone, "referenced");
    int rc = mount(clone.c_str(), mnt.c_str(), "zfs", by_prop ? 0 : MS_RDONLY, "");
    uint64_t w1 = prop(clone, "written"), r1 = prop(clone, "referenced");
    printf("%s: written %llu -> %llu, referenced %llu -> %llu\n", how.c_str(),
           (unsigned long long)w0, (unsigned long long)w1,
           (unsigned long long)r0, (unsigned long long)r1);
    check(rc == 0, how + ": mount");
    check(w1 == w0 && r1 == r0, how + ": the read-only mount left the dataset unchanged");
    umount(mnt.c_str());

    if (by_prop) {
        ch = p_zfs_open(hdl, clone.c_str(), ZFS_TYPE_FILESYSTEM);
        if (ch) {
            p_zfs_prop_set(ch, "readonly", "off");
            p_zfs_close(ch);
        }
    }
    rc = mount(clone.c_str(), mnt.c_str(), "zfs", 0, "");
    uint64_t r2 = prop(clone, "referenced");
    printf("%s: read-write mount, referenced %llu -> %llu\n", how.c_str(),
           (unsigned long long)r1, (unsigned long long)r2);
    check(rc == 0 && r2 + (file_mib << 20) / 2 < r1,
          how + ": the read-write mount drained the unlinked file");
    umount(mnt.c_str());
}

int main()
{
    if (!load_libzfs() || !(hdl = p_libzfs_init())) {
        printf("SKIP: libzfs is not usable\n");
        fflush(stdout);
        return 0;
    }
    for (const char* c : {"rpool", "osv", "data"}) {
        zpool_handle_t* ph = p_zpool_open(hdl, c);
        if (ph) {
            p_zpool_close(ph);
            pool = c;
            break;
        }
    }
    if (pool.empty()) {
        printf("SKIP: no ZFS pool found\n");
        fflush(stdout);
        return 0;
    }

    std::string ds = pool + "/unlro", snap = ds + "@s";
    std::string ca = pool + "/unlro-prop", cb = pool + "/unlro-mnt";
    destroy(ca, ZFS_TYPE_FILESYSTEM);
    destroy(cb, ZFS_TYPE_FILESYSTEM);
    destroy(snap, ZFS_TYPE_SNAPSHOT);
    destroy(ds, ZFS_TYPE_FILESYSTEM);

    bool ok = make_snapshot(ds, snap);
    check(ok, "snapshot " + snap + " holds an unlinked file");
    if (ok) {
        run_case(snap, ca, true);
        run_case(snap, cb, false);
    }
    destroy(ca, ZFS_TYPE_FILESYSTEM);
    destroy(cb, ZFS_TYPE_FILESYSTEM);
    destroy(snap, ZFS_TYPE_SNAPSHOT);
    destroy(ds, ZFS_TYPE_FILESYSTEM);
    p_libzfs_fini(hdl);
    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    fflush(stdout);
    return failures ? 1 : 0;
}
