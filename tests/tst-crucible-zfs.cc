/*
 * Copyright (C) 2026 OSv Authors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * tst-crucible-zfs: end-to-end ZFS-on-Crucible test that exercises the
 * Crucible block driver under a real ZFS workload.
 *
 * Skipped cleanly if /dev/crucible0 is not present (the boot path
 * leaves it absent when --crucible= is not passed to run.py).
 *
 * Test sequence:
 *   1. Open libzfs.so via dlopen.
 *   2. Build an nvlist describing a single-disk pool on /dev/crucible0.
 *   3. zpool_create("tank", nvroot, ...).
 *   4. Open the pool, create a dataset tank/test, mount it.
 *   5. Write a 1 MiB file and fsync.
 *   6. Read it back and verify byte-for-byte.
 *   7. Destroy dataset and pool.
 *
 * The test deliberately avoids the zpool(8) binary because the OSv
 * userspace zpool entry point currently has a stale-pointer bug in
 * its argv handling (independent of Crucible) that causes a SIGSEGV
 * in strlen() shortly after main().  The libzfs API does not go
 * through that path.
 *
 * Run:
 *   ./scripts/run.py -k --arch=x86_64 -m 2048 -c1 \
 *     --crucible=HOST:P1,HOST:P2,HOST:P3 --crucible-uuid=... \
 *     -e "tests/tst-crucible-zfs.so"
 */

#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

constexpr const char *DEV       = "/dev/crucible0";
constexpr const char *POOL_NAME = "ctank";
constexpr const char *DATASET   = "ctank/test";
constexpr const char *MOUNTPT   = "/ctank-test";
constexpr const char *TEST_FILE = "/ctank-test/payload.bin";
/*
 * Test workload sizes, in ascending order of stress.  Each size is a
 * multiple of the ZFS recordsize (128 KiB by default), so every entry
 * exercises the multi-record fan-out path.  The largest sizes drive
 * thousands of records through the pipeline.
 */
constexpr size_t WORKLOAD_SIZES[] = {
    256ull   * 1024,        /*    2 records      ~256 KiB  */
    1ull     * 1024 * 1024, /*    8 records      1 MiB     */
    4ull     * 1024 * 1024, /*   32 records      4 MiB     */
    16ull    * 1024 * 1024, /*  128 records      16 MiB    */
    64ull    * 1024 * 1024, /*  512 records      64 MiB    */
    256ull   * 1024 * 1024, /* 2048 records      256 MiB   */
};
constexpr size_t NUM_WORKLOAD_SIZES =
    sizeof(WORKLOAD_SIZES) / sizeof(WORKLOAD_SIZES[0]);

int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define PASS(fmt, ...) do {                                  \
    tests_run++; tests_passed++;                              \
    printf("  PASS  " fmt "\n", ##__VA_ARGS__);               \
} while (0)

#define FAIL(fmt, ...) do {                                  \
    tests_run++; tests_failed++;                              \
    printf("  FAIL  " fmt "\n", ##__VA_ARGS__);               \
} while (0)

/* libzfs/libnvpair runtime bindings via dlopen. */
typedef struct libzfs_handle libzfs_handle_t;
typedef struct zfs_handle    zfs_handle_t;
typedef struct zpool_handle  zpool_handle_t;
typedef struct nvlist        nvlist_t;

#define ZFS_TYPE_FILESYSTEM   (1 << 0)

typedef libzfs_handle_t *(*fn_libzfs_init)(void);
typedef void             (*fn_libzfs_fini)(libzfs_handle_t *);
typedef const char *     (*fn_libzfs_error_description)(libzfs_handle_t *);
typedef int              (*fn_zpool_create)(libzfs_handle_t *, const char *,
                                            nvlist_t *, nvlist_t *, nvlist_t *);
typedef zpool_handle_t * (*fn_zpool_open)(libzfs_handle_t *, const char *);
typedef void             (*fn_zpool_close)(zpool_handle_t *);
typedef int              (*fn_zpool_destroy)(zpool_handle_t *, const char *);
typedef int              (*fn_zfs_create)(libzfs_handle_t *, const char *,
                                          int, nvlist_t *);
typedef zfs_handle_t *   (*fn_zfs_open)(libzfs_handle_t *, const char *, int);
typedef int              (*fn_zfs_mount)(zfs_handle_t *, const char *, int);
typedef int              (*fn_zfs_unmount)(zfs_handle_t *, const char *, int);
typedef int              (*fn_zfs_destroy)(zfs_handle_t *, int);
typedef void             (*fn_zfs_close)(zfs_handle_t *);

typedef nvlist_t *(*fn_fnvlist_alloc)(void);
typedef void      (*fn_fnvlist_free)(nvlist_t *);
typedef void      (*fn_fnvlist_add_string)(nvlist_t *, const char *,
                                            const char *);
typedef void      (*fn_fnvlist_add_nvlist_array)(nvlist_t *, const char *,
                                                  const nvlist_t * const *,
                                                  unsigned int);
typedef void      (*fn_fnvlist_add_uint64)(nvlist_t *, const char *,
                                            uint64_t);

static fn_libzfs_init               p_libzfs_init;
static fn_libzfs_fini               p_libzfs_fini;
static fn_libzfs_error_description  p_libzfs_error_description;
static fn_zpool_create              p_zpool_create;
static fn_zpool_open                p_zpool_open;
static fn_zpool_close               p_zpool_close;
static fn_zpool_destroy             p_zpool_destroy;
static fn_zfs_create                p_zfs_create;
static fn_zfs_open                  p_zfs_open;
static fn_zfs_mount                 p_zfs_mount;
static fn_zfs_unmount               p_zfs_unmount;
static fn_zfs_destroy               p_zfs_destroy;
static fn_zfs_close                 p_zfs_close;
static fn_fnvlist_alloc             p_fnvlist_alloc;
static fn_fnvlist_free              p_fnvlist_free;
static fn_fnvlist_add_string        p_fnvlist_add_string;
static fn_fnvlist_add_nvlist_array  p_fnvlist_add_nvlist_array;
static fn_fnvlist_add_uint64        p_fnvlist_add_uint64;

bool load_libzfs()
{
    void *hzfs = dlopen("libzfs.so", RTLD_NOW | RTLD_GLOBAL);
    if (!hzfs) {
        FAIL("dlopen libzfs.so: %s", dlerror());
        return false;
    }

#define L(handle, sym)                                                  \
    do {                                                                \
        p_##sym = reinterpret_cast<fn_##sym>(dlsym(handle, #sym));      \
        if (!p_##sym) {                                                 \
            FAIL("dlsym %s: %s", #sym, dlerror());                      \
            return false;                                               \
        }                                                               \
    } while (0)

    L(hzfs, libzfs_init);
    L(hzfs, libzfs_fini);
    L(hzfs, libzfs_error_description);
    L(hzfs, zpool_create);
    L(hzfs, zpool_open);
    L(hzfs, zpool_close);
    L(hzfs, zpool_destroy);
    L(hzfs, zfs_create);
    L(hzfs, zfs_open);
    L(hzfs, zfs_mount);
    L(hzfs, zfs_unmount);
    L(hzfs, zfs_destroy);
    L(hzfs, zfs_close);

    /* fnvlist_* live in libnvpair.so, loaded transitively with RTLD_GLOBAL. */
    L(RTLD_DEFAULT, fnvlist_alloc);
    L(RTLD_DEFAULT, fnvlist_free);
    L(RTLD_DEFAULT, fnvlist_add_string);
    L(RTLD_DEFAULT, fnvlist_add_nvlist_array);
    L(RTLD_DEFAULT, fnvlist_add_uint64);
#undef L
    return true;
}

/*
 * Build an nvroot for a single-disk pool on /dev/crucible0.
 *
 *   nvroot {
 *     "type" = "root"
 *     "children" = [ {
 *       "type" = "disk"
 *       "path" = "/dev/crucible0"
 *     } ]
 *   }
 */
nvlist_t *make_nvroot(const char *dev)
{
    nvlist_t *vdev = p_fnvlist_alloc();
    p_fnvlist_add_string(vdev, "type", "disk");
    p_fnvlist_add_string(vdev, "path", dev);

    /*
     * Optional ashift override (CRUCIBLE_ZFS_ASHIFT).  Crucible volumes
     * use 4096-byte blocks (ashift=12); a plain virtio disk defaults to
     * 512 (ashift=9).  Forcing ashift=12 on virtio reproduces the 4K
     * vdev geometry without Crucible, isolating whether a zpool_create
     * crash is tied to ashift=12 or to the Crucible driver itself.
     */
    const char *ashift = getenv("CRUCIBLE_ZFS_ASHIFT");
    if (ashift && *ashift) {
        p_fnvlist_add_uint64(vdev, "ashift",
                             strtoull(ashift, nullptr, 10));
    }

    nvlist_t *root = p_fnvlist_alloc();
    p_fnvlist_add_string(root, "type", "root");
    const nvlist_t *children[] = { vdev };
    p_fnvlist_add_nvlist_array(root, "children", children, 1);

    p_fnvlist_free(vdev);
    return root;
}

struct io_result {
    size_t bytes;
    double write_secs;
    double read_secs;
    bool   ok;
};

// The payload byte at absolute file offset i is a pure function of i, so the
// workload can be streamed in bounded chunks without ever holding the whole
// file in memory.  This keeps peak heap at O(CHUNK_BYTES) rather than
// O(workload), which is what lets the 256 MiB sweep run under a 128 MiB guest.
static inline uint8_t payload_byte(size_t i)
{
    return static_cast<uint8_t>((i * 31u + (i >> 11)) ^ 0xA7);
}

bool run_io_roundtrip(size_t bytes, io_result *out)
{
    out->bytes = bytes;
    out->ok = false;

    constexpr size_t CHUNK_BYTES = 1ull * 1024 * 1024;
    uint8_t *buf = static_cast<uint8_t *>(malloc(CHUNK_BYTES));
    if (!buf) {
        FAIL("malloc(%zu) failed", CHUNK_BYTES);
        return false;
    }

    int fd = open(TEST_FILE, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        FAIL("open(%s) failed: %s", TEST_FILE, strerror(errno));
        free(buf);
        return false;
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t off = 0; off < bytes; off += CHUNK_BYTES) {
        size_t chunk = (bytes - off < CHUNK_BYTES) ? (bytes - off) : CHUNK_BYTES;
        for (size_t j = 0; j < chunk; j++) {
            buf[j] = payload_byte(off + j);
        }
        ssize_t n = write(fd, buf, chunk);
        if (n != static_cast<ssize_t>(chunk)) {
            FAIL("write returned %zd at off %zu (errno=%d %s)",
                 n, off, errno, strerror(errno));
            close(fd); free(buf);
            return false;
        }
    }
    if (fsync(fd) != 0) {
        FAIL("fsync failed: %s", strerror(errno));
        close(fd); free(buf);
        return false;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    out->write_secs = std::chrono::duration<double>(t1 - t0).count();

    /* Re-open to bypass any in-process buffering. */
    close(fd);
    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) {
        FAIL("re-open(%s): %s", TEST_FILE, strerror(errno));
        free(buf);
        return false;
    }

    auto r0 = std::chrono::high_resolution_clock::now();
    bool mismatch = false;
    size_t first_diff = 0;
    uint8_t got_byte = 0, exp_byte = 0;
    for (size_t off = 0; off < bytes; off += CHUNK_BYTES) {
        size_t chunk = (bytes - off < CHUNK_BYTES) ? (bytes - off) : CHUNK_BYTES;
        ssize_t n = read(fd, buf, chunk);
        if (n != static_cast<ssize_t>(chunk)) {
            auto r1 = std::chrono::high_resolution_clock::now();
            out->read_secs = std::chrono::duration<double>(r1 - r0).count();
            FAIL("read returned %zd at off %zu", n, off);
            close(fd); free(buf);
            return false;
        }
        for (size_t j = 0; j < chunk; j++) {
            if (buf[j] != payload_byte(off + j)) {
                mismatch = true;
                first_diff = off + j;
                got_byte = buf[j];
                exp_byte = payload_byte(off + j);
                break;
            }
        }
        if (mismatch) break;
    }
    auto r1 = std::chrono::high_resolution_clock::now();
    out->read_secs = std::chrono::duration<double>(r1 - r0).count();

    if (mismatch) {
        printf("  data mismatch at offset %zu: expected %02x got %02x\n",
               first_diff, exp_byte, got_byte);
        FAIL("data mismatch (offset %zu)", first_diff);
        close(fd); free(buf);
        return false;
    }
    close(fd);
    free(buf);
    out->ok = true;
    return true;
}

/*
 * Run an ascending sweep of workload sizes, fsync between each.  This
 * is the integration-level "thousands of records" exercise: at 256 MiB
 * with the default 128 KiB recordsize, each iteration crosses 2048 ZFS
 * records and the surrounding 3-replica quorum write path.
 */
bool run_io_sweep()
{
    printf("\n  %-10s  %-12s  %-12s\n", "size", "write MB/s", "read MB/s");
    printf("  %-10s  %-12s  %-12s\n", "----", "----------", "---------");
    bool all_ok = true;
    for (size_t i = 0; i < NUM_WORKLOAD_SIZES; i++) {
        io_result r;
        if (!run_io_roundtrip(WORKLOAD_SIZES[i], &r)) {
            all_ok = false;
            break;
        }
        double write_mbps = (r.bytes / (1024.0 * 1024.0)) / r.write_secs;
        double read_mbps  = (r.bytes / (1024.0 * 1024.0)) / r.read_secs;
        const char *unit = (r.bytes >= 1024ull * 1024) ? "MiB" : "KiB";
        size_t shown = (r.bytes >= 1024ull * 1024) ? r.bytes / (1024 * 1024)
                                                    : r.bytes / 1024;
        printf("  %4zu %s    %10.1f    %10.1f\n",
               shown, unit, write_mbps, read_mbps);
    }
    if (all_ok) {
        PASS("workload sweep across %zu sizes (largest = %zu MiB / 2048 records)",
             NUM_WORKLOAD_SIZES,
             WORKLOAD_SIZES[NUM_WORKLOAD_SIZES - 1] / (1024 * 1024));
    }
    return all_ok;
}

} /* anonymous namespace */

int main()
{
    printf("=== ZFS-on-Crucible end-to-end test ===\n\n");

    /*
     * Device under test defaults to /dev/crucible0 but can be overridden
     * via CRUCIBLE_ZFS_DEV.  Pointing it at a plain virtio scratch disk
     * (/dev/vblk1) isolates whether a zpool_create crash is Crucible-
     * specific or a generic OSv guest-side zpool_create bug.
     */
    const char *dev = getenv("CRUCIBLE_ZFS_DEV");
    if (!dev || !*dev) {
        dev = DEV;
    }

    /* Skip if no volume. */
    {
        struct stat st;
        if (stat(dev, &st) != 0) {
            printf("SKIP: %s does not exist (boot without --crucible= ?)\n", dev);
            return 0;
        }
    }
    PASS("%s exists", dev);

    if (!load_libzfs()) return 1;

    libzfs_handle_t *zfsh = p_libzfs_init();
    if (!zfsh) {
        FAIL("libzfs_init returned NULL");
        return 1;
    }

    /* If the pool already exists from a previous run, destroy it. */
    {
        zpool_handle_t *zh = p_zpool_open(zfsh, POOL_NAME);
        if (zh) {
            (void) p_zpool_destroy(zh, "tst-crucible-zfs cleanup");
            p_zpool_close(zh);
            PASS("pre-existing %s pool destroyed", POOL_NAME);
        }
    }

    /* zpool_create(handle, name, nvroot, props=NULL, fsprops=NULL). */
    nvlist_t *nvroot = make_nvroot(dev);
    int rc = p_zpool_create(zfsh, POOL_NAME, nvroot, nullptr, nullptr);
    p_fnvlist_free(nvroot);
    if (rc != 0) {
        FAIL("zpool_create(%s) returned %d (%s)", POOL_NAME, rc,
             p_libzfs_error_description(zfsh));
        p_libzfs_fini(zfsh);
        return 1;
    }
    PASS("zpool_create(%s) on %s", POOL_NAME, dev);

    /* Create dataset with mountpoint. */
    {
        nvlist_t *props = p_fnvlist_alloc();
        p_fnvlist_add_string(props, "mountpoint", MOUNTPT);
        rc = p_zfs_create(zfsh, DATASET, ZFS_TYPE_FILESYSTEM, props);
        p_fnvlist_free(props);
        if (rc != 0) {
            FAIL("zfs_create(%s) returned %d (%s)", DATASET, rc,
                 p_libzfs_error_description(zfsh));
            goto cleanup;
        }
        PASS("zfs_create(%s)", DATASET);

        zfs_handle_t *zh = p_zfs_open(zfsh, DATASET, ZFS_TYPE_FILESYSTEM);
        if (!zh) {
            FAIL("zfs_open(%s) returned NULL", DATASET);
            goto cleanup;
        }
        rc = p_zfs_mount(zh, nullptr, 0);
        p_zfs_close(zh);
        if (rc != 0) {
            FAIL("zfs_mount returned %d", rc);
            goto cleanup;
        }
        PASS("zfs_mount(%s) at %s", DATASET, MOUNTPT);
    }

    /* Sweep workload sizes from 256 KiB to 256 MiB on the dataset. */
    run_io_sweep();

    /* Cleanup: unmount + destroy dataset, destroy pool. */
    {
        zfs_handle_t *zh = p_zfs_open(zfsh, DATASET, ZFS_TYPE_FILESYSTEM);
        if (zh) {
            (void) p_zfs_unmount(zh, nullptr, 0);
            (void) p_zfs_destroy(zh, 0);
            p_zfs_close(zh);
            PASS("zfs_destroy(%s)", DATASET);
        }
    }

cleanup:
    {
        zpool_handle_t *zh = p_zpool_open(zfsh, POOL_NAME);
        if (zh) {
            (void) p_zpool_destroy(zh, "test cleanup");
            p_zpool_close(zh);
            PASS("zpool_destroy(%s)", POOL_NAME);
        }
    }

    p_libzfs_fini(zfsh);

    printf("\n=== Results: %d/%d passed (%d failed) ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
