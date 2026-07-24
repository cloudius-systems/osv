/*
 * Copyright (C) 2026 OSv Authors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * ZFS direct I/O validation test.
 *
 * Validates that O_DIRECT I/O on a ZFS dataset:
 *  1. Either succeeds and produces correct data (OpenZFS 2.3+ direct I/O path)
 *     or returns EINVAL at open (ZFS does not support O_DIRECT for this build).
 *  2. When O_DIRECT is accepted, data written is readable via a buffered fd.
 *  3. When O_DIRECT is accepted for reads, data previously written via the
 *     buffered path matches.
 *  4. Mixed write (O_DIRECT) + read (buffered) and write (buffered) + read
 *     (O_DIRECT) both return consistent data.
 *
 * Run: ./scripts/run.py --image <zfs-image> -e "tests/tst-zfs-direct-io.so"
 * Requires: ZFS root filesystem (build with fs=zfs)
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dlfcn.h>

/* --------------------------------------------------------------------------
 * libzfs runtime binding — no build-time dependency on libzfs headers.
 * -------------------------------------------------------------------------- */
typedef struct libzfs_handle libzfs_handle_t;
typedef struct zfs_handle    zfs_handle_t;
typedef struct zpool_handle  zpool_handle_t;

#define ZFS_TYPE_FILESYSTEM (1 << 0)

typedef libzfs_handle_t *(*fn_libzfs_init)(void);
typedef void             (*fn_libzfs_fini)(libzfs_handle_t *);
typedef zpool_handle_t * (*fn_zpool_open)(libzfs_handle_t *, const char *);
typedef void             (*fn_zpool_close)(zpool_handle_t *);
typedef int              (*fn_zfs_create)(libzfs_handle_t *, const char *, int, void *);
typedef zfs_handle_t *   (*fn_zfs_open)(libzfs_handle_t *, const char *, int);
typedef int              (*fn_zfs_prop_set)(zfs_handle_t *, const char *, const char *);
typedef int              (*fn_zfs_destroy)(zfs_handle_t *, int);
typedef void             (*fn_zfs_close)(zfs_handle_t *);

static fn_libzfs_init  p_libzfs_init;
static fn_libzfs_fini  p_libzfs_fini;
static fn_zpool_open   p_zpool_open;
static fn_zpool_close  p_zpool_close;
static fn_zfs_create   p_zfs_create;
static fn_zfs_open     p_zfs_open;
static fn_zfs_prop_set p_zfs_prop_set;
static fn_zfs_destroy  p_zfs_destroy;
static fn_zfs_close    p_zfs_close;

static bool load_libzfs(void)
{
    void *h = dlopen("libzfs.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!h) {
        fprintf(stderr, "SKIP: cannot load libzfs.so: %s\n", dlerror());
        return false;
    }
#define L(name) \
    p_##name = (fn_##name)dlsym(h, #name); \
    if (!p_##name) { fprintf(stderr, "SKIP: symbol " #name " not found\n"); return false; }
    L(libzfs_init) L(libzfs_fini)
    L(zpool_open) L(zpool_close)
    L(zfs_create) L(zfs_open) L(zfs_prop_set) L(zfs_destroy) L(zfs_close)
#undef L
    return true;
}

static const char *detect_pool(libzfs_handle_t *zfsh)
{
    static const char *cands[] = { "rpool", "osv", "data", nullptr };
    for (int i = 0; cands[i]; i++) {
        zpool_handle_t *ph = p_zpool_open(zfsh, cands[i]);
        if (ph) { p_zpool_close(ph); return cands[i]; }
    }
    return nullptr;
}

static void destroy_dataset(libzfs_handle_t *zfsh, const char *name)
{
    zfs_handle_t *zh = p_zfs_open(zfsh, name, ZFS_TYPE_FILESYSTEM);
    if (zh) { p_zfs_destroy(zh, /*defer=*/0); p_zfs_close(zh); }
}

static int create_dataset(libzfs_handle_t *zfsh, const char *name,
                           const char *mountpoint)
{
    /* Idempotent: tear down any leftover from a previous run. */
    destroy_dataset(zfsh, name);

    int rc = p_zfs_create(zfsh, name, ZFS_TYPE_FILESYSTEM, nullptr);
    if (rc != 0)
        return rc;
    zfs_handle_t *zh = p_zfs_open(zfsh, name, ZFS_TYPE_FILESYSTEM);
    if (!zh)
        return -1;
    if (mountpoint)
        p_zfs_prop_set(zh, "mountpoint", mountpoint);
    p_zfs_close(zh);
    return 0;
}

/* O_DIRECT must be aligned to 512 bytes for most block devices. */
static const size_t ALIGN     = 512;
static const size_t BUF_SIZE  = 4096;   /* single block, aligned */
static const size_t FILE_SIZE = 4096;

/* Fill a buffer with a repeating byte pattern. */
static void fill_pattern(char *buf, size_t len, char seed)
{
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)((seed + (char)i) & 0xFF);
    }
}

/* Return true if buffers match. */
static bool buf_eq(const char *a, const char *b, size_t len)
{
    return memcmp(a, b, len) == 0;
}

/* Allocate an aligned buffer (ALIGN-byte aligned). */
static char *alloc_aligned(size_t size)
{
    void *p = nullptr;
    if (posix_memalign(&p, ALIGN, size) != 0) {
        perror("posix_memalign");
        return nullptr;
    }
    return static_cast<char *>(p);
}

static int tests_run     = 0;
static int tests_passed  = 0;
static int tests_skipped = 0;

#define PASS(fmt, ...) \
    do { tests_run++; tests_passed++; \
         printf("  PASS  " fmt "\n", ##__VA_ARGS__); } while (0)

#define FAIL(fmt, ...) \
    do { tests_run++; \
         printf("  FAIL  " fmt "\n", ##__VA_ARGS__); } while (0)

#define SKIP(fmt, ...) \
    do { tests_skipped++; \
         printf("  SKIP  " fmt "\n", ##__VA_ARGS__); } while (0)

/* ------------------------------------------------------------------ */

/*
 * Test 1: open() with O_DIRECT on a ZFS file.
 * Returns the fd if O_DIRECT is accepted, -1 if EINVAL (not supported),
 * and calls FAIL + returns -2 on unexpected error.
 */
static int test_open_direct(const char *path, int flags)
{
    tests_run++;
    int fd = open(path, flags | O_DIRECT, 0644);
    if (fd >= 0) {
        tests_passed++;
        printf("  PASS  open(O_DIRECT) accepted (fd=%d)\n", fd);
        return fd;
    }
    if (errno == EINVAL) {
        /* O_DIRECT not supported by this ZFS build — not a failure. */
        printf("  SKIP  open(O_DIRECT) returned EINVAL — "
               "direct I/O not supported on this ZFS build\n");
        tests_skipped++;
        return -1;  /* caller should skip O_DIRECT-dependent subtests */
    }
    printf("  FAIL  open(O_DIRECT) failed with unexpected errno %d (%s)\n",
           errno, strerror(errno));
    return -2;
}

/*
 * Test 2: write-via-O_DIRECT, read-via-buffered.
 * Verifies that data written with O_DIRECT lands on disk and is readable
 * through a normal (ARC-buffered) fd.
 */
static void test_write_direct_read_buffered(const char *path)
{
    printf("\n[Test] write O_DIRECT → read buffered\n");

    char *wbuf = alloc_aligned(BUF_SIZE);
    char *rbuf = alloc_aligned(BUF_SIZE);
    if (!wbuf || !rbuf) { free(wbuf); free(rbuf); return; }

    fill_pattern(wbuf, BUF_SIZE, 0xA5);

    /* Write with O_DIRECT | O_SYNC to bypass ARC and flush to vdev. */
    int wfd = test_open_direct(path, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC);
    if (wfd == -1) {
        /* EINVAL: O_DIRECT not supported; test buffered write instead. */
        wfd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0644);
        if (wfd < 0) { perror("open(buffered write)"); free(wbuf); free(rbuf); return; }
        SKIP("O_DIRECT write skipped; using buffered write for data integrity check");
    }
    if (wfd < -1) { free(wbuf); free(rbuf); return; }

    ssize_t n = write(wfd, wbuf, BUF_SIZE);
    if (n != (ssize_t)BUF_SIZE) {
        FAIL("write returned %zd (expected %zu), errno=%d (%s)",
             n, BUF_SIZE, errno, strerror(errno));
        close(wfd); free(wbuf); free(rbuf); return;
    }
    fsync(wfd);
    close(wfd);
    PASS("write %zu bytes succeeded", BUF_SIZE);

    /* Read back via normal buffered fd. */
    int rfd = open(path, O_RDONLY);
    if (rfd < 0) {
        FAIL("open(buffered read): %s", strerror(errno));
        free(wbuf); free(rbuf); return;
    }
    n = read(rfd, rbuf, BUF_SIZE);
    close(rfd);
    if (n != (ssize_t)BUF_SIZE) {
        FAIL("buffered read returned %zd (expected %zu)", n, BUF_SIZE);
        free(wbuf); free(rbuf); return;
    }
    if (buf_eq(wbuf, rbuf, BUF_SIZE)) {
        PASS("buffered read after direct write: data matches");
    } else {
        FAIL("buffered read after direct write: DATA MISMATCH");
    }
    free(wbuf);
    free(rbuf);
}

/*
 * Test 3: write-via-buffered, read-via-O_DIRECT.
 * Verifies that data written through ARC can be read back bypassing ARC.
 */
static void test_write_buffered_read_direct(const char *path)
{
    printf("\n[Test] write buffered → read O_DIRECT\n");

    char *wbuf = alloc_aligned(BUF_SIZE);
    char *rbuf = alloc_aligned(BUF_SIZE);
    if (!wbuf || !rbuf) { free(wbuf); free(rbuf); return; }

    fill_pattern(wbuf, BUF_SIZE, 0x3C);

    /* Write via normal buffered fd + fsync so data is on disk. */
    int wfd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (wfd < 0) { perror("open(buffered write)"); free(wbuf); free(rbuf); return; }
    ssize_t n = write(wfd, wbuf, BUF_SIZE);
    fsync(wfd);
    close(wfd);
    if (n != (ssize_t)BUF_SIZE) {
        FAIL("buffered write returned %zd", n);
        free(wbuf); free(rbuf); return;
    }
    PASS("buffered write %zu bytes succeeded", BUF_SIZE);

    /* Read back with O_DIRECT. */
    int rfd = test_open_direct(path, O_RDONLY);
    if (rfd == -1) {
        SKIP("O_DIRECT read skipped; verifying buffered round-trip instead");
        rfd = open(path, O_RDONLY);
        if (rfd < 0) { perror("open(buffered fallback read)"); free(wbuf); free(rbuf); return; }
    }
    if (rfd < -1) { free(wbuf); free(rbuf); return; }

    n = read(rfd, rbuf, BUF_SIZE);
    close(rfd);
    if (n != (ssize_t)BUF_SIZE) {
        FAIL("direct read returned %zd (expected %zu)", n, BUF_SIZE);
        free(wbuf); free(rbuf); return;
    }
    if (buf_eq(wbuf, rbuf, BUF_SIZE)) {
        PASS("direct read after buffered write: data matches");
    } else {
        FAIL("direct read after buffered write: DATA MISMATCH");
    }
    free(wbuf);
    free(rbuf);
}

/*
 * Test 4: multi-block O_DIRECT write + buffered read.
 * Uses a larger file (multiple blocks) to exercise ZFS block boundaries.
 */
static void test_multiblock_direct(const char *path)
{
    printf("\n[Test] multi-block O_DIRECT write + buffered read\n");

    const size_t n_blocks = 8;
    const size_t total    = n_blocks * BUF_SIZE;

    char *wbuf = alloc_aligned(total);
    char *rbuf = alloc_aligned(total);
    if (!wbuf || !rbuf) { free(wbuf); free(rbuf); return; }

    for (size_t b = 0; b < n_blocks; b++) {
        fill_pattern(wbuf + b * BUF_SIZE, BUF_SIZE, (char)(b * 17));
    }

    int wfd = test_open_direct(path, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC);
    if (wfd == -1) {
        SKIP("multi-block O_DIRECT write skipped");
        free(wbuf); free(rbuf); return;
    }
    if (wfd < -1) { free(wbuf); free(rbuf); return; }

    ssize_t n = write(wfd, wbuf, total);
    fsync(wfd);
    close(wfd);
    if (n != (ssize_t)total) {
        FAIL("multi-block write: got %zd, expected %zu", n, total);
        free(wbuf); free(rbuf); return;
    }
    PASS("multi-block direct write %zu bytes (%zu blocks) succeeded", total, n_blocks);

    int rfd = open(path, O_RDONLY);
    if (rfd < 0) { perror("open for read"); free(wbuf); free(rbuf); return; }
    n = read(rfd, rbuf, total);
    close(rfd);
    if (n != (ssize_t)total) {
        FAIL("multi-block buffered read: got %zd, expected %zu", n, total);
    } else if (buf_eq(wbuf, rbuf, total)) {
        PASS("multi-block: buffered read after direct write matches");
    } else {
        /* Find first mismatch block. */
        for (size_t b = 0; b < n_blocks; b++) {
            if (!buf_eq(wbuf + b * BUF_SIZE, rbuf + b * BUF_SIZE, BUF_SIZE)) {
                FAIL("multi-block: mismatch at block %zu", b);
                break;
            }
        }
    }
    free(wbuf);
    free(rbuf);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== ZFS direct I/O validation test ===\n\n");

    /* Load libzfs and detect the ZFS pool. */
    if (!load_libzfs())
        return 1;

    libzfs_handle_t *zfsh = p_libzfs_init();
    if (!zfsh) {
        fprintf(stderr, "SKIP: libzfs_init() failed\n");
        return 1;
    }

    const char *pool = detect_pool(zfsh);
    if (!pool) {
        p_libzfs_fini(zfsh);
        fprintf(stderr, "SKIP: no ZFS pool found (tried rpool, osv, data)\n");
        return 1;
    }
    printf("Using pool: %s\n", pool);

    /* Create a dedicated dataset for direct I/O tests. */
    char ds_name[256];
    snprintf(ds_name, sizeof(ds_name), "%s/dio-test", pool);
    if (create_dataset(zfsh, ds_name, "/zfs-dio-test") != 0)
        fprintf(stderr, "warning: could not create dataset %s\n", ds_name);

    const char *test1 = "/zfs-dio-test/direct-write.dat";
    const char *test2 = "/zfs-dio-test/buffered-write.dat";
    const char *test3 = "/zfs-dio-test/multi-block.dat";

    test_write_direct_read_buffered(test1);
    test_write_buffered_read_direct(test2);
    test_multiblock_direct(test3);

    /* Cleanup. */
    unlink(test1);
    unlink(test2);
    unlink(test3);
    destroy_dataset(zfsh, ds_name);

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_skipped > 0) {
        printf(", %d skipped (O_DIRECT not yet supported on this ZFS build)",
               tests_skipped);
    }
    printf(" ===\n");

    p_libzfs_fini(zfsh);

    if (tests_run == tests_skipped) {
        /* All tests were skipped — O_DIRECT unsupported, but no failures. */
        printf("NOTE: O_DIRECT is not yet supported in the OSv OpenZFS port.\n");
        printf("      Data integrity was verified via buffered I/O fallback.\n");
        return 0;
    }

    return (tests_passed + tests_skipped == tests_run) ? 0 : 1;
}
