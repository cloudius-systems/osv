/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// An unforced unmount of a ZFS dataset must fail with EBUSY while a vnode of
// that file system is still alive, even one that no open file or dentry
// holds. The page cache keeps such a vnode: a page written through a
// MAP_SHARED mapping stays in its write cache, holding the file's vnode,
// after munmap() and close(), until the cache evicts the page and writes it
// back. If the unmount went ahead, that writeback would use the vnode after
// the unmount freed its znode and the mount.
//
// The test writes a page through a MAP_SHARED mapping of a file on a child
// dataset, unmaps and closes it, and checks that the unmount fails with
// EBUSY. It then makes the page cache evict that page, by writing many pages
// of another file through a shared mapping, and checks that the unmount now
// succeeds and that the page reached the file.

#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
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

// The page cache keeps at most max(1% of memory, 100) pages written through
// shared mappings, and evicts the oldest when it has more.
static size_t evict_pages()
{
    return std::max<size_t>(sysconf(_SC_PHYS_PAGES) / 100, 100) * 2;
}

// Write one byte through a MAP_SHARED mapping of path, then unmap and close.
static bool write_shared(const std::string& path, size_t pages, char c)
{
    int fd = open(path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0 || ftruncate(fd, pages * 4096) != 0) {
        report(false, "create " + path + ": " + strerror(errno));
        if (fd >= 0) {
            close(fd);
        }
        return false;
    }
    void *p = mmap(nullptr, pages * 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        report(false, "mmap " + path + ": " + strerror(errno));
        close(fd);
        return false;
    }
    for (size_t i = 0; i < pages; i++) {
        static_cast<char *>(p)[i * 4096] = c;
    }
    bool ok = munmap(p, pages * 4096) == 0 && close(fd) == 0;
    report(ok, "write " + std::to_string(pages) + " page(s) of " + path + " through MAP_SHARED");
    return ok;
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
    const std::string ds = std::string(pool) + "/umount-busy", dir = "/umount-busy";
    const std::string file = dir + "/f", evict = "/tmp/umount-busy-evict";
    destroy(h, ds);
    if (create(h, ds) && mount_ds(ds, dir) && write_shared(file, 1, 'y')) {
        // Check that the page cache's vnode keeps the mount busy.
        int r = umount(dir.c_str());
        int err = errno;
        report(r == -1 && err == EBUSY, "umount " + dir + " fails with EBUSY while the page cache holds a vnode (got " +
               (r == 0 ? std::string("success") : std::string(strerror(err))) + ")");
        if (r == 0) {
            // The unmount went ahead; evicting the page now would write it
            // back through the freed znode.
            printf("SUMMARY: %d tests, %d failures\n", tests, fails);
            return 1;
        }
        // Check that after the page cache drops the page, the unmount works
        // and the page was written to the file.
        write_shared(evict, evict_pages(), 'e');
        unlink(evict.c_str());
        report(umount(dir.c_str()) == 0, "umount " + dir + " succeeds once the page cache dropped the page");
        if (mount_ds(ds, dir)) {
            char c = 0;
            int fd = open(file.c_str(), O_RDONLY);
            report(fd >= 0 && read(fd, &c, 1) == 1 && c == 'y', "the page written through MAP_SHARED reached " + file);
            if (fd >= 0) {
                close(fd);
            }
            report(umount(dir.c_str()) == 0, "umount " + dir + " after reading the file");
        }
    }
    destroy(h, ds);
    rmdir(dir.c_str());
    p_libzfs_fini(h);
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return fails ? 1 : 0;
}
