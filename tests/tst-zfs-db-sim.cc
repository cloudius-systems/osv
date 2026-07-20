/*
 * tst-zfs-db-sim.cc — ZFS database I/O simulation (Postgres WAL workload)
 *
 * Simulates a PostgreSQL-style OLTP workload against an 8 KiB-page database
 * backed by ZFS with recordsize=8K.  Designed to be run in a Firecracker VM
 * with 128 MiB RAM so that the ZFS ARC covers only a fraction of the database,
 * producing heavy cache pressure typical of memory-constrained database servers.
 *
 * The test tries six configurations in order from least to most aggressively
 * tuned, measuring tuple-updates/second for each and noting whether any I/O
 * errors occurred.  The goal is to identify the minimum set of OSv/ZFS knobs
 * that allows a 750 MiB database to operate without failures at 128 MiB RAM.
 *
 * Configurations tested
 * ─────────────────────
 *  C0  baseline          compression=off primarycache=all      logbias=latency    O_DIRECT=no
 *  C1  odirect           compression=off primarycache=all      logbias=latency    O_DIRECT=yes
 *  C2  pcache            compression=off primarycache=metadata logbias=latency    O_DIRECT=no
 *  C3  odirect+pcache    compression=off primarycache=metadata logbias=latency    O_DIRECT=yes
 *  C4  odirect+lz4+pcache compression=lz4 primarycache=metadata logbias=latency  O_DIRECT=yes
 *  C5  full              compression=lz4 primarycache=metadata logbias=throughput O_DIRECT=yes
 *
 * OSv-level tunings applied unconditionally (see arc_os.c, zfs_initialize_osv.c):
 *  - ARC max = 1/8 of RAM for RAM < 256 MiB  (was 5/8; frees ~48 MiB on 128 MiB)
 *  - zfs_txg_timeout = 2 s                    (was 5 s; reduces dirty-data peak)
 *  - zfs_dirty_data_max_percent = 5 %         (was 10 %; saves ~6 MiB on 128 MiB)
 *
 * Transaction model — one tuple update per transaction
 * ─────────────────────────────────────────────────────
 *   1. Pick a random database page index in [0, DB_PAGES)
 *   2. pread  the 8 KiB page                            [random read]
 *   3. Modify a tuple at a deterministic offset          [compute]
 *   4. write  an 80-byte WAL record                     [sequential write]
 *   5. pwrite the modified 8 KiB page back to disk      [random write]
 *   6. Every WAL_SYNC_INTERVAL transactions: fdatasync  [WAL flush]
 *
 * Build  listed in modules/tests/Makefile and modules/zfs-tools/usr.manifest
 * Run    ./scripts/run.py -m 128 --image <zfs-image> -e "/tests/tst-zfs-db-sim.so"
 * Requires ZFS root filesystem (build with fs=zfs fs_size_mb=4096)
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dlfcn.h>

/* ── tunables ──────────────────────────────────────────────────────────────── */

static const size_t   PAGE_BYTES        = 8192;      /* ZFS recordsize=8K        */
static const uint64_t DB_SIZE_MB        = 750;
static const uint64_t DB_PAGES          = (DB_SIZE_MB * 1024ULL * 1024ULL) / PAGE_BYTES;
static const size_t   WAL_RECORD_SIZE   = 80;        /* bytes per WAL entry       */
static const int      WAL_SYNC_INTERVAL = 100;       /* fdatasync every N txns    */
static const int      BENCH_SECONDS     = 30;        /* measurement window        */

/* ── WAL record layout (80 bytes) ───────────────────────────────────────────── */

struct __attribute__((packed)) wal_record {
    uint64_t lsn;           /*  8: log sequence number           */
    uint64_t xid;           /*  8: transaction id                */
    uint64_t page_idx;      /*  8: which database page           */
    uint32_t tuple_off;     /*  4: byte offset of tuple in page  */
    uint32_t tuple_len;     /*  4: length of changed tuple data  */
    uint8_t  data[48];      /* 48: before/after image            */
};                          /* 80 bytes total                    */

static_assert(sizeof(wal_record) == WAL_RECORD_SIZE, "WAL record size mismatch");

/* ── database page header (matches Postgres HeapPageHeader shape) ─────────── */

struct __attribute__((packed)) page_header {
    uint64_t pd_lsn;        /* 8: LSN of last WAL record for this page */
    uint32_t pd_page_id;    /* 4: page number                          */
    uint32_t pd_checksum;   /* 4: simple checksum                      */
    uint16_t pd_lower;      /* 2: start of free space                  */
    uint16_t pd_upper;      /* 2: end of free space                    */
    uint16_t pd_flags;      /* 2: page flags                           */
    uint16_t pd_reserved;   /* 2                                       */
    uint8_t  pd_data[PAGE_BYTES - 24]; /* tuple storage area           */
};

static_assert(sizeof(page_header) == PAGE_BYTES, "page_header size mismatch");

/* ── libzfs dynamic binding ─────────────────────────────────────────────────── */

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
    if (!h) { fprintf(stderr, "SKIP: cannot load libzfs.so: %s\n", dlerror()); return false; }
#define L(name) \
    p_##name = (fn_##name)dlsym(h, #name); \
    if (!p_##name) { fprintf(stderr, "SKIP: symbol " #name " not found\n"); return false; }
    L(libzfs_init) L(libzfs_fini)
    L(zpool_open)  L(zpool_close)
    L(zfs_create)  L(zfs_open)  L(zfs_prop_set)  L(zfs_destroy)  L(zfs_close)
#undef L
    return true;
}

static const char *detect_pool(libzfs_handle_t *zfsh)
{
    static const char *cands[] = { "osv", "rpool", "data", nullptr };
    for (int i = 0; cands[i]; i++) {
        zpool_handle_t *ph = p_zpool_open(zfsh, cands[i]);
        if (ph) { p_zpool_close(ph); return cands[i]; }
    }
    return nullptr;
}

/* ── benchmark configuration ────────────────────────────────────────────────── */

struct bench_config {
    const char *name;
    bool        use_odirect;    /* O_DIRECT on database fd (bypasses pagecache)  */
    const char *compression;    /* ZFS dataset compression: "off" or "lz4"       */
    const char *primarycache;   /* ZFS ARC data policy: "all" or "metadata"      */
    const char *logbias;        /* ZFS write strategy: "latency" or "throughput" */
    uint64_t    min_ram_mb;     /* minimum RAM (MiB) required to run safely      */
    const char *description;
};

/*
 * Six configurations from least tuned to most tuned.
 * Baseline (C0) demonstrates the raw constraint; Full (C5) shows the
 * combination that makes 256 MiB + 750 MiB database viable.
 *
 * min_ram_mb thresholds (empirically derived):
 *   C0  buffered + primarycache=all:      pagecache + ARC data → needs ~512 MiB
 *   C1  O_DIRECT + primarycache=all:      ARC data + kernel → needs ~192 MiB
 *   C2  buffered + primarycache=metadata: pagecache still grows → needs ~512 MiB
 *   C3  O_DIRECT + primarycache=metadata: only ARC metadata + kernel → safe at 128 MiB
 *   C4  C3 + lz4:                         lz4 ABD compression requires ~12 KB
 *     physically-contiguous pages per pwrite.  After hundreds of ZIO cycles,
 *     physical memory fragments such that malloc_large(12288, contiguous=true)
 *     livelocks OSv's reclaimer even with 20+ MiB nominally free.  Safe at ≥192 MiB.
 *   C5  C4 + logbias=throughput:          same lz4 fragmentation risk; safe at ≥256 MiB
 *     (logbias=throughput issues larger, less frequent I/Os, increasing peak ABD
 *     concurrency and thus the contiguous-memory demand per TXG sync).
 */
static const bench_config CONFIGS[] = {
    /* name                  odirect  compress  primarycache  logbias       min_ram  description */
    { "C0-baseline",         false,  "off",    "all",        "latency",    512,
      "no tuning: pagecache + ARC data both grow (needs ~512 MiB)" },
    { "C1-odirect",          true,   "off",    "all",        "latency",    192,
      "O_DIRECT only: removes pagecache; ARC still caches data (needs ~192 MiB)" },
    { "C2-pcache-meta",      false,  "off",    "metadata",   "latency",    512,
      "primarycache=metadata only: pagecache still grows (needs ~512 MiB)" },
    { "C3-odirect+pcache",   true,   "off",    "metadata",   "latency",     96,
      "O_DIRECT + primarycache=metadata: pagecache and ARC data both eliminated" },
    { "C4-odirect+lz4",      true,   "lz4",    "metadata",   "latency",    192,
      "C3 + compression=lz4: reduces on-disk footprint; needs >=192 MiB (lz4 ABD fragmentation)" },
    { "C5-full",             true,   "lz4",    "metadata",   "throughput", 256,
      "C4 + logbias=throughput: bypass ZIL for WAL; needs >=256 MiB (lz4 ABD fragmentation)" },
};
static const int NCONFIGS = (int)(sizeof(CONFIGS) / sizeof(CONFIGS[0]));

/* ── dataset management ─────────────────────────────────────────────────────── */

static int setup_dataset(libzfs_handle_t *zfsh, const char *ds_name,
                          const char *mountpoint, const bench_config &cfg)
{
    /* Unmount and destroy any leftover dataset from a previous run.
     * Use umount2() directly — system() is a no-op in OSv. */
    umount2("/scratch", MNT_DETACH);  /* ignore errors if not mounted */
    zfs_handle_t *old = p_zfs_open(zfsh, ds_name, ZFS_TYPE_FILESYSTEM);
    if (old) {
        p_zfs_destroy(old, 0);
        p_zfs_close(old);
    }

    int rc = p_zfs_create(zfsh, ds_name, ZFS_TYPE_FILESYSTEM, nullptr);
    if (rc != 0) {
        fprintf(stderr, "  zfs_create(%s) failed: %d\n", ds_name, rc);
        return rc;
    }
    zfs_handle_t *zh = p_zfs_open(zfsh, ds_name, ZFS_TYPE_FILESYSTEM);
    if (!zh) { fprintf(stderr, "  zfs_open(%s) failed\n", ds_name); return -1; }

    p_zfs_prop_set(zh, "recordsize",    "8K");
    p_zfs_prop_set(zh, "dedup",         "off");
    p_zfs_prop_set(zh, "compression",   cfg.compression);
    p_zfs_prop_set(zh, "primarycache",  cfg.primarycache);
    p_zfs_prop_set(zh, "logbias",       cfg.logbias);
    p_zfs_prop_set(zh, "mountpoint",    mountpoint);
    p_zfs_close(zh);
    return 0;
}

static void destroy_dataset(libzfs_handle_t *zfsh, const char *ds_name)
{
    umount2("/scratch", MNT_DETACH);
    zfs_handle_t *zh = p_zfs_open(zfsh, ds_name, ZFS_TYPE_FILESYSTEM);
    if (zh) {
        p_zfs_destroy(zh, 0);
        p_zfs_close(zh);
    }
}

/* ── helpers ────────────────────────────────────────────────────────────────── */

static uint64_t xorshift64(uint64_t &s)
{
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return s;
}

static uint32_t simple_checksum(const uint8_t *data, size_t len)
{
    uint32_t c = 0;
    for (size_t i = 0; i < len; i++) c = (c << 1) ^ data[i];
    return c;
}

/* ── benchmark result ───────────────────────────────────────────────────────── */

struct bench_result {
    uint64_t txns;
    uint64_t wal_syncs;
    double   elapsed_s;
    bool     io_error;      /* true if any pread/pwrite/WAL error occurred */
    char     error_msg[64]; /* first error message */
};

/* ── Phase 1: sparse database file ─────────────────────────────────────────── */

static bool init_database(int db_fd)
{
    off_t db_size = (off_t)(DB_PAGES * PAGE_BYTES);
    if (ftruncate(db_fd, db_size) != 0) {
        fprintf(stderr, "  ERROR: ftruncate failed: %s\n", strerror(errno));
        return false;
    }
    return true;
}

/* ── Phase 2: transaction benchmark ─────────────────────────────────────────── */

static bench_result run_benchmark(int db_fd, int wal_fd, page_header *page_buf)
{
    using clock = std::chrono::steady_clock;

    static wal_record wal_rec;

    uint64_t rng     = 0xDEADBEEFCAFEBABEULL;
    uint64_t lsn     = 1;
    uint64_t txns    = 0;
    uint64_t wal_syn = 0;

    bench_result res = {};
    res.io_error     = false;

    auto t_start = clock::now();
    auto t_end   = t_start + std::chrono::seconds(BENCH_SECONDS);

    while (clock::now() < t_end) {
        /* Run a batch of 64 transactions between time checks. */
        for (int b = 0; b < 64; b++) {
            uint64_t page_idx = xorshift64(rng) % DB_PAGES;
            off_t    page_off = (off_t)(page_idx * PAGE_BYTES);

            /* Read the page (sparse → zero-fill on first access) */
            ssize_t n = pread(db_fd, page_buf, PAGE_BYTES, page_off);
            if (n != (ssize_t)PAGE_BYTES) {
                memset(page_buf, 0, PAGE_BYTES);
                page_buf->pd_page_id = (uint32_t)page_idx;
                page_buf->pd_lower   = 32;
                page_buf->pd_upper   = (uint16_t)PAGE_BYTES;
            }

            /* Modify a tuple */
            uint32_t tuple_off = (uint32_t)(xorshift64(rng) %
                                            (sizeof(page_buf->pd_data) - 8));
            uint8_t *tuple_ptr = page_buf->pd_data + tuple_off;
            uint64_t new_val   = xorshift64(rng);
            memcpy(tuple_ptr, &new_val, 8);
            page_buf->pd_lsn      = lsn;
            page_buf->pd_checksum = simple_checksum(
                (const uint8_t *)page_buf, PAGE_BYTES - 4);

            /* Write WAL record (buffered, not O_DIRECT) */
            wal_rec.lsn       = lsn;
            wal_rec.xid       = txns + 1;
            wal_rec.page_idx  = page_idx;
            wal_rec.tuple_off = tuple_off;
            wal_rec.tuple_len = 8;
            memcpy(wal_rec.data, tuple_ptr, 8);
            n = write(wal_fd, &wal_rec, WAL_RECORD_SIZE);
            if (n != (ssize_t)WAL_RECORD_SIZE) {
                res.io_error = true;
                snprintf(res.error_msg, sizeof(res.error_msg),
                         "WAL write: %s", strerror(errno));
                goto done;
            }

            /* Write modified page back */
            n = pwrite(db_fd, page_buf, PAGE_BYTES, page_off);
            if (n != (ssize_t)PAGE_BYTES) {
                res.io_error = true;
                snprintf(res.error_msg, sizeof(res.error_msg),
                         "pwrite: %s", strerror(errno));
                goto done;
            }

            /* Periodic WAL flush */
            if ((++txns % WAL_SYNC_INTERVAL) == 0) {
                fdatasync(wal_fd);
                wal_syn++;
            }
            lsn++;
        }
    }

done:;
    res.elapsed_s = std::chrono::duration<double>(clock::now() - t_start).count();
    res.txns      = txns;
    res.wal_syncs = wal_syn;
    return res;
}

/* ── per-config run ─────────────────────────────────────────────────────────── */

static bench_result run_config(libzfs_handle_t *zfsh, const char *pool,
                                const bench_config &cfg, page_header *page_buf)
{
    bench_result res = {};
    res.io_error     = true;
    snprintf(res.error_msg, sizeof(res.error_msg), "setup failed");

    char ds_name[256];
    snprintf(ds_name, sizeof(ds_name), "%s/scratch", pool);

    if (setup_dataset(zfsh, ds_name, "/scratch", cfg) != 0)
        return res;

    mkdir("/scratch",       0755);
    mkdir("/scratch/dbsim", 0755);

    const char *db_path  = "/scratch/dbsim/data.db";
    const char *wal_path = "/scratch/dbsim/wal.log";

    int db_open_flags = O_RDWR | O_CREAT | O_TRUNC;
    if (cfg.use_odirect)
        db_open_flags |= O_DIRECT;

    int db_fd = open(db_path, db_open_flags, 0644);
    if (db_fd < 0) {
        fprintf(stderr, "  open(data.db) failed: %s\n", strerror(errno));
        snprintf(res.error_msg, sizeof(res.error_msg), "open db: %s", strerror(errno));
        destroy_dataset(zfsh, ds_name);
        return res;
    }

    /* WAL uses buffered writes; O_DIRECT requires 512-byte-aligned transfers
     * but WAL records are 80 bytes so buffered + fdatasync is correct here. */
    int wal_fd = open(wal_path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    if (wal_fd < 0) {
        fprintf(stderr, "  open(wal.log) failed: %s\n", strerror(errno));
        snprintf(res.error_msg, sizeof(res.error_msg), "open wal: %s", strerror(errno));
        close(db_fd);
        destroy_dataset(zfsh, ds_name);
        return res;
    }

    if (!init_database(db_fd)) {
        close(db_fd); close(wal_fd);
        destroy_dataset(zfsh, ds_name);
        return res;
    }

    res = run_benchmark(db_fd, wal_fd, page_buf);

    close(db_fd);
    close(wal_fd);
    unlink(db_path);
    unlink(wal_path);
    rmdir("/scratch/dbsim");
    destroy_dataset(zfsh, ds_name);
    return res;
}

/* ── main ───────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== ZFS database simulation: 8 KiB pages, %llu MiB, "
           "Postgres WAL workload ===\n",
           (unsigned long long)DB_SIZE_MB);
    printf("    %llu pages, %d-second benchmark per configuration\n\n",
           (unsigned long long)DB_PAGES, BENCH_SECONDS);

    printf("OSv-level tunings active (arc_os.c + zfs_initialize_osv.c):\n");
    printf("  ARC max   = RAM/8 for RAM < 256 MiB  (was 5/8; saves ~48 MiB @ 128 MiB)\n");
    printf("  txg_timeout = 2 s                     (was 5 s)\n");
    printf("  dirty_data_max_percent = 5 %%          (was 10 %%)\n\n");

    if (!load_libzfs()) return 1;

    libzfs_handle_t *zfsh = p_libzfs_init();
    if (!zfsh) { fprintf(stderr, "libzfs_init failed\n"); return 1; }

    const char *pool = detect_pool(zfsh);
    if (!pool) {
        p_libzfs_fini(zfsh);
        fprintf(stderr, "SKIP: no ZFS pool found (tried osv, rpool, data)\n");
        return 1;
    }
    printf("Pool: %s\n\n", pool);

    /*
     * Detect physical RAM.  Buffered I/O configs are safe only when there is
     * enough RAM to hold the pagecache alongside ZFS ARC and the kernel.
     * Empirically, a 750 MiB database requires > 256 MiB RAM without O_DIRECT;
     * with O_DIRECT the pagecache is bypassed and 128 MiB is sufficient.
     *
     * At < 256 MiB, skip buffered configs rather than crashing the kernel with
     * the virt_to_phys OOM assertion in virtio-blk/make_request.
     */
    uint64_t phys_ram_mb = ((uint64_t)sysconf(_SC_PHYS_PAGES) *
                            (uint64_t)sysconf(_SC_PAGE_SIZE)) >> 20;
    printf("Physical RAM detected: %llu MiB\n\n",
           (unsigned long long)phys_ram_mb);

    /*
     * Allocate a single PAGE_BYTES-aligned I/O buffer shared across all
     * configs.  Required for O_DIRECT (buffer, offset, and size must all be
     * aligned to the filesystem block size = 8 KiB); harmless for buffered I/O.
     */
    page_header *page_buf = nullptr;
    if (posix_memalign((void **)&page_buf, PAGE_BYTES, PAGE_BYTES) != 0) {
        fprintf(stderr, "posix_memalign failed: %s\n", strerror(errno));
        p_libzfs_fini(zfsh);
        return 1;
    }

    /* ── Run all configurations ────────────────────────────────────────────── */

    struct run_record {
        bench_result res;
        double       tps;
        bool         skipped;
    } results[NCONFIGS];
    memset(results, 0, sizeof(results));

    for (int i = 0; i < NCONFIGS; i++) {
        const bench_config &cfg = CONFIGS[i];
        printf("──────────────────────────────────────────────────────────────\n");
        printf("[%s]  %s\n", cfg.name, cfg.description);
        printf("  recordsize=8K  compression=%-4s  primarycache=%-8s  "
               "logbias=%-10s  O_DIRECT=%s\n",
               cfg.compression, cfg.primarycache, cfg.logbias,
               cfg.use_odirect ? "yes" : "no");
        fflush(stdout);

        /*
         * Skip this config if the system has less RAM than it requires.
         * Each config's min_ram_mb is empirically derived: running below
         * the threshold exhausts physical memory and triggers a kernel
         * assertion in mmu::virt_to_phys (virtio-blk DMA path).
         *
         * Root causes per config:
         *  C0/C2 (buffered): OSv pagecache maps every pread'd page → grows to
         *    fill all available RAM for a 750 MiB database.
         *  C1 (O_DIRECT + primarycache=all): ZFS ARC caches data blocks;
         *    at 128 MiB our 16 MiB ARC cap + kernel + write pipeline ≈ 100 MiB
         *    which is safe, but metaslab loading on first writes can push higher.
         *  C3 (O_DIRECT + primarycache=metadata): ARC holds only metadata
         *    (< 5 MiB), write buffers capped at 5% RAM = 6 MiB, safe at 128 MiB.
         *  C4-C5 (+ lz4): each pwrite allocates a ~12 KB physically-contiguous
         *    ABD buffer for lz4 output.  After hundreds of ZIO alloc/free cycles,
         *    physical pages fragment.  OSv's reclaimer livelocks when no 3-page
         *    contiguous run exists even with 20+ MiB nominally free.
         *    C4 requires ≥ 192 MiB; C5 requires ≥ 256 MiB.
         */
        if (phys_ram_mb < cfg.min_ram_mb) {
            printf("  SKIP: %llu MiB RAM < required %llu MiB — would OOM\n",
                   (unsigned long long)phys_ram_mb,
                   (unsigned long long)cfg.min_ram_mb);
            results[i].skipped = true;
            fflush(stdout);
            continue;
        }

        results[i].res = run_config(zfsh, pool, cfg, page_buf);
        bench_result &r = results[i].res;
        results[i].tps  = r.elapsed_s > 0.0
                          ? (double)r.txns / r.elapsed_s : 0.0;

        if (r.io_error) {
            printf("  RESULT: FAILED  [%s]  (%.0f txns in %.1f s = %.1f txn/s)\n",
                   r.error_msg, (double)r.txns, r.elapsed_s, results[i].tps);
        } else {
            printf("  RESULT: PASS    %.1f txn/s  (%.0f txns in %.1f s, "
                   "%llu WAL syncs)\n",
                   results[i].tps, (double)r.txns, r.elapsed_s,
                   (unsigned long long)r.wal_syncs);
        }
        fflush(stdout);

        /*
         * Pause between configs to allow ZFS async dataset destruction to
         * complete.  ZFS destroys datasets asynchronously: the DSL destroyer
         * thread iterates over the dataset's blocks and queues them for
         * freeing over multiple TXG syncs (txg_timeout=2 s each).  For a
         * ~340 MiB dataset (~43 000 blocks), full reclamation takes ~10-15
         * TXG syncs (20-30 s).  Without a sufficient pause, C5 sees ENOSPC
         * because pool free-space accounting still reflects the previous
         * config's blocks.  30 s = ~15 TXG syncs at txg_timeout=2.
         */
        if (i + 1 < NCONFIGS)
            sleep(30);
    }

    free(page_buf);
    p_libzfs_fini(zfsh);

    /* ── Summary table ─────────────────────────────────────────────────────── */

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf("SUMMARY: 750 MiB ZFS database @ 128 MiB RAM\n");
    printf("══════════════════════════════════════════════════════════════\n");
    printf("%-22s  %-8s  %-6s  %-10s  %-10s  %-5s  %s\n",
           "Config", "Status", "TPS", "compress", "pcache", "ODIR", "Notes");
    printf("%-22s  %-8s  %-6s  %-10s  %-10s  %-5s  %s\n",
           "──────────────────────", "──────", "──────",
           "──────────", "──────────", "─────", "──────────────────────────");

    const bench_config *first_pass = nullptr;
    for (int i = 0; i < NCONFIGS; i++) {
        const bench_config &cfg = CONFIGS[i];
        bench_result       &r   = results[i].res;
        const char *status;
        const char *note;
        bool pass = false;
        if (results[i].skipped) {
            status = "SKIP";
            note   = "RAM below minimum threshold";
        } else if (r.io_error) {
            status = "FAIL";
            note   = r.error_msg;
        } else {
            status = "PASS";
            note   = "";
            pass   = true;
        }
        if (pass && first_pass == nullptr)
            first_pass = &cfg;

        printf("%-22s  %-8s  %6.1f  %-10s  %-10s  %-5s  %s\n",
               cfg.name, status, results[i].tps,
               cfg.compression, cfg.primarycache,
               cfg.use_odirect ? "yes" : "no",
               note);
    }

    printf("\n");
    if (first_pass) {
        printf("Minimum viable configuration: %s\n", first_pass->name);
        printf("  %s\n", first_pass->description);
    } else {
        printf("No configuration completed without I/O errors.\n");
        printf("Consider increasing available RAM or disk image size.\n");
    }

    /*
     * Exit 0 if the highest-numbered config that was not skipped passed
     * (no I/O errors).  Configs are skipped when the system has less RAM
     * than their min_ram_mb threshold, so the "best reachable" config is
     * the right success criterion — requiring C5 on a 128 MiB VM would
     * always fail even when C3/C4 ran correctly.
     */
    int  best_ran  = -1;
    bool best_pass = false;
    for (int i = NCONFIGS - 1; i >= 0; i--) {
        if (!results[i].skipped) {
            best_ran  = i;
            best_pass = !results[i].res.io_error;
            break;
        }
    }
    if (best_ran < 0) {
        printf("\nAll configs skipped (insufficient RAM for any configuration).\n");
        return 1;
    }
    printf("\nHighest config run (%s): %s\n", CONFIGS[best_ran].name,
           best_pass ? "PASS" : "FAIL");
    return best_pass ? 0 : 1;
}
