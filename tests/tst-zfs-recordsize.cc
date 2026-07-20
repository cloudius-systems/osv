/*
 * Copyright (C) 2026 OSv Authors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * ZFS recordsize benchmark: compare sequential I/O throughput with 8kB vs 128kB recordsize.
 *
 * ZFS 'recordsize' controls the minimum I/O block size per dataset:
 *   - 8K:   more suited to small random I/O (databases); higher metadata overhead
 *   - 128K: ZFS default (SPA_OLD_MAXBLOCKSIZE); optimal for sequential large-file I/O
 *
 * This test creates two datasets on the ZFS root pool with different recordsizes,
 * then measures sequential write and read throughput for each.
 *
 * Dataset management uses the libzfs C API loaded at runtime via dlopen() so that
 * the test does not depend on fork/exec (which OSv does not support).
 *
 * Run: ./scripts/run.py --image <zfs-image> -e "tests/tst-zfs-recordsize.so"
 * Requires: ZFS root filesystem (build with fs=zfs) and /libzfs.so in the image.
 */

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

static const size_t FILE_SIZE = 128UL * 1024 * 1024;  // 128 MB per test run

/* --------------------------------------------------------------------------
 * libzfs runtime binding
 * We use opaque pointer types so we need no libzfs headers at build time.
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
    if (!p_##name) { fprintf(stderr, "SKIP: symbol " #name " not found: %s\n", dlerror()); return false; }

    L(libzfs_init)
    L(libzfs_fini)
    L(zpool_open)
    L(zpool_close)
    L(zfs_create)
    L(zfs_open)
    L(zfs_prop_set)
    L(zfs_destroy)
    L(zfs_close)
#undef L
    return true;
}

/* Try pool names in order; return first one that opens successfully. */
static const char *detect_pool(libzfs_handle_t *zfsh)
{
    static const char *candidates[] = { "rpool", "osv", "data", nullptr };
    for (int i = 0; candidates[i]; i++) {
        zpool_handle_t *ph = p_zpool_open(zfsh, candidates[i]);
        if (ph) {
            p_zpool_close(ph);
            return candidates[i];
        }
    }
    return nullptr;
}

/*
 * Create <pool>/<suffix> with default properties, then set recordsize.
 * ZFS automounts the dataset at /<suffix> (inheriting the pool's "/" mountpoint).
 */
static int create_bench_ds(libzfs_handle_t *zfsh, const char *pool,
                            const char *suffix, const char *recordsize)
{
    char name[256];
    snprintf(name, sizeof(name), "%s/%s", pool, suffix);

    int rc = p_zfs_create(zfsh, name, ZFS_TYPE_FILESYSTEM, nullptr);
    if (rc != 0)
        return rc;

    zfs_handle_t *zh = p_zfs_open(zfsh, name, ZFS_TYPE_FILESYSTEM);
    if (!zh)
        return -1;

    p_zfs_prop_set(zh, "recordsize", recordsize);
    p_zfs_close(zh);
    return 0;
}

static void destroy_bench_ds(libzfs_handle_t *zfsh, const char *pool,
                              const char *suffix)
{
    char name[256];
    snprintf(name, sizeof(name), "%s/%s", pool, suffix);

    zfs_handle_t *zh = p_zfs_open(zfsh, name, ZFS_TYPE_FILESYSTEM);
    if (zh) {
        p_zfs_destroy(zh, /*defer=*/0);
        p_zfs_close(zh);
    }
}

/* --------------------------------------------------------------------------
 * Benchmark
 * -------------------------------------------------------------------------- */
struct bench_result {
    double write_mbs;
    double read_mbs;
};

static bench_result run_one(const char *filepath, size_t io_size)
{
    using clk = std::chrono::high_resolution_clock;
    std::vector<char> buf(io_size, 0xAB);
    bench_result r = {};

    /* Sequential write */
    {
        int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("open for write");
            return r;
        }
        auto t0 = clk::now();
        for (size_t done = 0; done < FILE_SIZE; ) {
            ssize_t n = write(fd, buf.data(), buf.size());
            if (n <= 0) { perror("write"); break; }
            done += (size_t)n;
        }
        fsync(fd);
        close(fd);
        double elapsed = std::chrono::duration<double>(clk::now() - t0).count();
        r.write_mbs = (double)FILE_SIZE / (1024.0 * 1024.0) / elapsed;
    }

    /* Sequential read */
    {
        int fd = open(filepath, O_RDONLY);
        if (fd < 0) {
            perror("open for read");
            return r;
        }
        auto t0 = clk::now();
        for (size_t done = 0; done < FILE_SIZE; ) {
            ssize_t n = read(fd, buf.data(), buf.size());
            if (n <= 0) break;
            done += (size_t)n;
        }
        close(fd);
        double elapsed = std::chrono::duration<double>(clk::now() - t0).count();
        r.read_mbs = (double)FILE_SIZE / (1024.0 * 1024.0) / elapsed;
    }

    unlink(filepath);
    return r;
}

int main(void)
{
    printf("=== ZFS recordsize benchmark: 8kB vs 128kB ===\n");
    printf("Test file size: %zu MB  (I/O buffer = recordsize)\n\n",
           FILE_SIZE / (1024 * 1024));

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
    printf("Using pool: %s\n\n", pool);

    /* Create benchmark datasets.
     * ZFS mounts <pool>/bench8k at /<pool>/bench8k (inherits pool mountpoint). */
    if (create_bench_ds(zfsh, pool, "bench8k",   "8K")   != 0)
        fprintf(stderr, "warning: could not create %s/bench8k\n",   pool);
    if (create_bench_ds(zfsh, pool, "bench128k", "128K") != 0)
        fprintf(stderr, "warning: could not create %s/bench128k\n", pool);

    /* Build file paths from dataset mountpoints. */
    char path8k[256], path128k[256];
    snprintf(path8k,   sizeof(path8k),   "/%s/bench8k/seq.dat",   pool);
    snprintf(path128k, sizeof(path128k), "/%s/bench128k/seq.dat", pool);

    struct {
        const char *label;
        const char *path;
        size_t      io_size;
    } cases[] = {
        { "8kB  recordsize",  path8k,   8 * 1024   },
        { "128kB recordsize", path128k, 128 * 1024 },
    };

    printf("  %-22s  %10s  %10s\n", "Configuration", "Write MB/s", "Read MB/s");
    printf("  %-22s  %10s  %10s\n", "-------------", "----------", "---------");

    for (auto &c : cases) {
        bench_result r = run_one(c.path, c.io_size);
        printf("  %-22s  %10.1f  %10.1f\n", c.label, r.write_mbs, r.read_mbs);
    }

    printf("\n");
    printf("Expected: 128kB recordsize shows higher sequential throughput.\n");
    printf("Reason: fewer ZFS I/O operations for the same data volume,\n");
    printf("        less metadata overhead, better block device utilization.\n");

    /* Cleanup */
    destroy_bench_ds(zfsh, pool, "bench8k");
    destroy_bench_ds(zfsh, pool, "bench128k");
    p_libzfs_fini(zfsh);

    return 0;
}
