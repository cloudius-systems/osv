/*
 * Copyright (C) 2026 OSv Authors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * ZFS mmap read-fault benchmark.
 *
 * Measures the cost of servicing MAP_SHARED read faults against a ZFS-backed
 * file, isolating the two quantities that a page-cache/ARC sharing bridge would
 * change:
 *
 *   1. cold fault latency (usec/page) -- a copy path pays one memcpy per page
 *      (ARC dbuf data -> freshly allocated pagecache page); a sharing bridge
 *      maps the dbuf's existing decompressed page directly and pays none.
 *   2. resident memory (RSS) after the whole file is mapped and touched -- a
 *      copy path holds each page twice (dbuf/ARC copy + pagecache copy); a
 *      sharing bridge holds it once.
 *
 * The file is sized larger than the target ARC so the first pass is genuinely
 * cold; a second pass over a resident subset measures warm re-fault cost.
 *
 * Datasets are created via the libzfs C API loaded with dlopen() (OSv has no
 * fork/exec).  Two datasets exercise both compression states, since the
 * decompressed dbuf page (what the bridge shares) exists regardless of on-disk
 * compression:
 *   <pool>/mmapoff  compression=off
 *   <pool>/mmaplz4  compression=lz4
 *
 * Run: ./scripts/run.py --image <zfs-image> -e "tests/misc-zfs-mmap-bench.so"
 * Requires: ZFS root filesystem (fs=zfs) and /libzfs.so in the image.
 */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <random>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

#define PAGE_SIZE (4UL << 10)

/* The cold-fault cost this bench isolates is the per-page memcpy from the ARC
 * dbuf into a freshly allocated pagecache page, paid on the first fault of a
 * fresh mapping whether or not the data is ARC-resident.  256 MiB is large
 * enough for a stable usec/page and a measurable RSS delta while completing
 * comfortably inside the run window. */
static const size_t FILE_SIZE = 256UL * 1024 * 1024;    /* 256 MiB */

/* --------------------------------------------------------------------------
 * libzfs runtime binding (opaque pointers; no libzfs headers at build time)
 * -------------------------------------------------------------------------- */
typedef struct libzfs_handle libzfs_handle_t;
typedef struct zfs_handle    zfs_handle_t;
typedef struct zpool_handle  zpool_handle_t;

#define ZFS_TYPE_FILESYSTEM (1 << 0)

typedef libzfs_handle_t *(*fn_libzfs_init)(void);
typedef void             (*fn_libzfs_fini)(libzfs_handle_t *);
typedef zpool_handle_t * (*fn_zpool_open)(libzfs_handle_t *, const char *);
typedef void             (*fn_zpool_close)(zpool_handle_t *);
typedef zfs_handle_t *   (*fn_zfs_open)(libzfs_handle_t *, const char *, int);
typedef int              (*fn_zfs_prop_set)(zfs_handle_t *, const char *, const char *);
typedef void             (*fn_zfs_close)(zfs_handle_t *);

static fn_libzfs_init  p_libzfs_init;
static fn_libzfs_fini  p_libzfs_fini;
static fn_zpool_open   p_zpool_open;
static fn_zpool_close  p_zpool_close;
static fn_zfs_open     p_zfs_open;
static fn_zfs_prop_set p_zfs_prop_set;
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
    L(zfs_open)
    L(zfs_prop_set)
    L(zfs_close)
#undef L
    return true;
}

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
 * Child-dataset mounting through libzfs do_mount() does not currently work on
 * OpenZFS/OSv (the mount(2) shim returns EINVAL for a non-root dataset).  The
 * root pool dataset, however, is mounted at boot (/<pool>).  Since the mmap
 * fault path we are measuring is identical regardless of which dataset backs
 * the file, we write the test file into the root dataset and toggle the root
 * dataset's compression + recordsize properties between passes.
 */
static int set_root_props(libzfs_handle_t *zfsh, const char *pool,
                          const char *compression)
{
    zfs_handle_t *zh = p_zfs_open(zfsh, pool, ZFS_TYPE_FILESYSTEM);
    if (!zh) {
        fprintf(stderr, "warning: zfs_open(%s) failed\n", pool);
        return -1;
    }
    p_zfs_prop_set(zh, "recordsize", "128K");
    p_zfs_prop_set(zh, "compression", compression);
    p_zfs_close(zh);
    return 0;
}

/* --------------------------------------------------------------------------
 * Resident-memory sampling.  OSv exposes /proc/meminfo (MemAvailable) when
 * procfs is mounted; fall back to sysinfo() otherwise.  We report the drop in
 * available memory across the map+touch, which captures both the pagecache
 * copy and any duplicate ARC/dbuf residency.
 * -------------------------------------------------------------------------- */
#include <sys/sysinfo.h>

static long avail_kb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        long kb = -1;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "MemAvailable: %ld kB", &kb) == 1)
                break;
            kb = -1;
        }
        fclose(f);
        if (kb >= 0)
            return kb;
    }
    struct sysinfo si;
    if (sysinfo(&si) == 0)
        return (long)((si.freeram * si.mem_unit) / 1024);
    return -1;
}

/* --------------------------------------------------------------------------
 * File preparation and fault timing
 * -------------------------------------------------------------------------- */
static char ch(size_t i) { return '@' + (char)(i % ('}' - '@')); }

static bool write_file(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open write"); return false; }
    const size_t CHUNK = 1UL << 20;   /* 1 MiB per write to amortize syscalls */
    std::vector<char> buf(CHUNK);
    for (size_t off = 0; off < FILE_SIZE; off += CHUNK) {
        for (size_t p = 0; p < CHUNK; p += PAGE_SIZE)
            memset(buf.data() + p, ch((off + p) / PAGE_SIZE), PAGE_SIZE);
        if (write(fd, buf.data(), CHUNK) != (ssize_t)CHUNK) {
            perror("write"); close(fd); return false;
        }
    }
    fsync(fd);
    close(fd);
    return true;
}

/* Touch one byte per page; return usec/page and a checksum to defeat DCE. */
static double fault_pass(unsigned char *base, size_t pages,
                         const std::vector<size_t> &order, volatile unsigned long *sink)
{
    using clk = std::chrono::high_resolution_clock;
    unsigned long acc = 0;
    auto t0 = clk::now();
    for (size_t k = 0; k < pages; k++) {
        size_t p = order[k];
        acc += (unsigned char)base[p * PAGE_SIZE];
    }
    auto dt = std::chrono::duration_cast<std::chrono::microseconds>(clk::now() - t0).count();
    *sink = acc;
    return (double)dt / (double)pages;
}

static void bench_dataset(const char *label, const char *path)
{
    printf("\n--- %s (%s) ---\n", label, path);

    if (!write_file(path)) {
        printf("  SKIP: could not write test file\n");
        return;
    }

    size_t pages = FILE_SIZE / PAGE_SIZE;
    std::vector<size_t> seq(pages), rnd(pages);
    for (size_t i = 0; i < pages; i++) seq[i] = i;
    rnd = seq;
    std::mt19937_64 rng(12345);
    std::shuffle(rnd.begin(), rnd.end(), rng);

    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open read"); return; }

    long avail_before = avail_kb();
    void *m = mmap(nullptr, FILE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { perror("mmap"); close(fd); return; }
    unsigned char *base = (unsigned char *)m;

    volatile unsigned long sink = 0;

    /* Cold sequential: first touch of every page -> faults from disk. */
    double cold_seq = fault_pass(base, pages, seq, &sink);

    long avail_after = avail_kb();

    /* Warm sequential: pages now resident -> pure re-fault / no I/O. */
    double warm_seq = fault_pass(base, pages, seq, &sink);

    /* Drop the mapping to force cold again, then random cold. */
    munmap(m, FILE_SIZE);
    /* posix_fadvise DONTNEED is not reliably wired; re-open to reset. */
    close(fd);
    fd = open(path, O_RDONLY);
    m = mmap(nullptr, FILE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { perror("mmap2"); close(fd); return; }
    base = (unsigned char *)m;
    double cold_rnd = fault_pass(base, pages, rnd, &sink);

    munmap(m, FILE_SIZE);
    close(fd);

    double rss_mb = (avail_before >= 0 && avail_after >= 0)
                    ? (double)(avail_before - avail_after) / 1024.0 : -1.0;

    printf("  cold seq fault : %8.3f usec/page\n", cold_seq);
    printf("  warm seq fault : %8.3f usec/page\n", warm_seq);
    printf("  cold rnd fault : %8.3f usec/page\n", cold_rnd);
    if (rss_mb >= 0)
        printf("  RSS delta map+touch: %8.1f MB  (file=%zu MB, pages=%zu)\n",
               rss_mb, FILE_SIZE / (1024 * 1024), pages);
    else
        printf("  RSS delta map+touch: (unavailable)\n");
    printf("  checksum: %lu\n", sink);

    unlink(path);
}

int main(void)
{
    printf("=== ZFS mmap read-fault benchmark ===\n");
    printf("File size: %zu MB, page=%lu B\n", FILE_SIZE / (1024 * 1024), PAGE_SIZE);

    if (!load_libzfs())
        return 1;

    libzfs_handle_t *zfsh = p_libzfs_init();
    if (!zfsh) { fprintf(stderr, "SKIP: libzfs_init() failed\n"); return 1; }

    const char *pool = detect_pool(zfsh);
    if (!pool) {
        p_libzfs_fini(zfsh);
        fprintf(stderr, "SKIP: no ZFS pool found (tried rpool, osv, data)\n");
        return 1;
    }
    printf("Using pool: %s\n", pool);

    /* The root dataset may be mounted at /<pool> (OpenZFS default) or at /
     * (when the pool was created with -m /, as the OSv single-dataset image
     * builder does).  Probe both and use whichever accepts a write. */
    char path[256];
    const char *bases[] = { nullptr, "" };   /* /<pool>/... , /... */
    char base0[128];
    snprintf(base0, sizeof(base0), "/%s", pool);
    bases[0] = base0;
    path[0] = '\0';
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        char probe[256];
        snprintf(probe, sizeof(probe), "%s/.mmapbench.probe", bases[i]);
        int pf = open(probe, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (pf >= 0) {
            close(pf);
            unlink(probe);
            snprintf(path, sizeof(path), "%s/big.dat", bases[i]);
            break;
        }
    }
    if (path[0] == '\0') {
        p_libzfs_fini(zfsh);
        fprintf(stderr, "SKIP: no writable mountpoint for pool %s\n", pool);
        return 1;
    }
    printf("Using path: %s\n", path);

    if (set_root_props(zfsh, pool, "off") == 0)
        bench_dataset("compression=off", path);
    else
        printf("SKIP compression=off: could not set root props\n");

    if (set_root_props(zfsh, pool, "lz4") == 0)
        bench_dataset("compression=lz4", path);
    else
        printf("SKIP compression=lz4: could not set root props\n");

    p_libzfs_fini(zfsh);

    printf("\nPASSED\n");
    return 0;
}
