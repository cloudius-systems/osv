/*
 * OpenZFS/BSD-ZFS-on-OSv microbenchmark harness (Phase E) -- PURE C.
 * Copyright (C) 2026 Greg Burd
 *
 * PURE C on purpose: a bare-host-g++ .so pulls libstdc++ symbols
 * (std::string@GLIBCXX_3.4.21, std::chrono::steady_clock, operator new/delete,
 * __cxa_* exception machinery) that OSv cannot resolve at .so load time ->
 * "Failed to load object: /zfs-bench.so. Powering off." So: only plain C here.
 *   - no std::string   -> char[] + snprintf/strcpy
 *   - no std::chrono   -> clock_gettime(CLOCK_MONOTONIC) + timespec math
 *   - no std::vector   -> fixed C arrays
 *   - no exceptions, no new/delete
 *   - integers parsed by hand (host glibc 2.38 strtoul emits __isoc23_* OSv
 *     also can't resolve).
 * Verify: `nm -D zfs-bench.so | grep -E "GLIBCXX|CXXABI|__isoc23"` must be EMPTY.
 *
 * Impl-agnostic: same .so runs under BSD-ZFS and OpenZFS builds (uses only the
 * generic /zpool.so + /zfs.so tools and POSIX file I/O). Self-inits /dev/zfs
 * (zfsdev_init), creates a pool over the passed vdev(s), makes a dataset, runs
 * one workload, prints `RESULT <name> ...` lines.
 *
 * Boot: zfs_builder.elf -m 8G <disks as virtio-blk>
 *   -append '--nomount --noinit --preload-zfs-library /zfs-bench.so <wl> [k=v...]'
 *
 * Usage: /zfs-bench.so <workload> [k=v ...]
 *   pool=bench recsize=1M comp=off atime=off size_mb=6144
 *   vdevs=/dev/vblk1                    (single-vdev)
 *   vdevs=raidz2:/dev/vblk1,/dev/vblk2,...(raidz2)
 *   qd=8 bs=4096 secs=30 reps=3 impl=openzfs direct=0 nfiles=100000
 * Workloads: seqwrite seqread_cold seqread_warm randread randwrite
 *            mmapread fsync meta compress scrub odirect info
 */
extern int osv_run_app(const char *app_path, const char *args[], int args_len);
extern void zfsdev_init(void);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* ---- timing (clock_gettime, no std::chrono) ---- */
static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ---- hand-rolled uint parse (no strtoul -> no __isoc23_*) ---- */
static unsigned long parse_ul(const char *s) {
    unsigned long v = 0;
    if (!s) return 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10UL + (unsigned long)(*s - '0'); s++; }
    return v;
}

/* ---- xorshift64 PRNG (no rand) ---- */
static uint64_t g_rng = 0x9e3779b97f4a7c15ULL;
static uint64_t xrand(void) {
    uint64_t x = g_rng;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return g_rng = x;
}

/* ---- run a /zpool.so or /zfs.so command ---- */
static int run_cmd(const char *path, const char *args[], int n) {
    int ret = osv_run_app(path, args, n);
    printf("  $ %s", path);
    for (int i = 0; i < n; i++) printf(" %s", args[i]);
    printf("   -> ret=%d\n", ret);
    return ret;
}

/* ---- options (fixed C strings) ---- */
typedef struct {
    char pool[64];
    char recsize[16];
    char comp[16];
    char atime[16];
    char vdevs[512];   /* single dev, or "raidz2:/dev/vblk1,/dev/vblk2,..." */
    char ds[128];
    unsigned long size_mb, qd, bs, secs_run, reps, nfiles, filesz_mb, direct;
    char impl[32];
} Opts;

static void set_str(char *dst, size_t cap, const char *v) {
    strncpy(dst, v, cap - 1); dst[cap - 1] = '\0';
}

static void parse_opts(Opts *o, int start, int ac, char **av) {
    for (int i = start; i < ac; i++) {
        char *kv = av[i];
        char *eq = strchr(kv, '=');
        if (!eq) continue;
        size_t klen = (size_t)(eq - kv);
        const char *v = eq + 1;
        char k[32];
        if (klen >= sizeof k) continue;
        memcpy(k, kv, klen); k[klen] = '\0';
        if      (!strcmp(k, "pool"))      set_str(o->pool, sizeof o->pool, v);
        else if (!strcmp(k, "recsize"))   set_str(o->recsize, sizeof o->recsize, v);
        else if (!strcmp(k, "comp"))      set_str(o->comp, sizeof o->comp, v);
        else if (!strcmp(k, "atime"))     set_str(o->atime, sizeof o->atime, v);
        else if (!strcmp(k, "vdevs"))     set_str(o->vdevs, sizeof o->vdevs, v);
        else if (!strcmp(k, "ds"))        set_str(o->ds, sizeof o->ds, v);
        else if (!strcmp(k, "impl"))      set_str(o->impl, sizeof o->impl, v);
        else if (!strcmp(k, "size_mb"))   o->size_mb = parse_ul(v);
        else if (!strcmp(k, "qd"))        o->qd = parse_ul(v);
        else if (!strcmp(k, "bs"))        o->bs = parse_ul(v);
        else if (!strcmp(k, "secs"))      o->secs_run = parse_ul(v);
        else if (!strcmp(k, "reps"))      o->reps = parse_ul(v);
        else if (!strcmp(k, "nfiles"))    o->nfiles = parse_ul(v);
        else if (!strcmp(k, "filesz_mb")) o->filesz_mb = parse_ul(v);
        else if (!strcmp(k, "direct"))    o->direct = parse_ul(v);
    }
}

/* ---- /dev/zfs + /etc/mnttab ---- */
static void ensure_zfs_dev(void) {
    zfsdev_init();
    mkdir("/etc", 0755);
    int fd = creat("/etc/mnttab", 0644);
    if (fd >= 0) close(fd);
}

/* ---- pool bring-up. vdevs is single dev or "raidzN:/d1,/d2,..." ---- */
#define MAXVDEV 16
static int create_pool(const Opts *o) {
    const char *a[8 + MAXVDEV];
    int n = 0;
    a[n++] = "zpool"; a[n++] = "create"; a[n++] = "-f";
    /* BSD-ZFS rejects the ashift pool prop; OpenZFS accepts -o ashift=12 */
    if (strcmp(o->impl, "bsd") != 0) { a[n++] = "-o"; a[n++] = "ashift=12"; }
    static char compopt[32], atimeopt[32];
    snprintf(compopt, sizeof compopt, "compression=%s", o->comp);
    snprintf(atimeopt, sizeof atimeopt, "atime=%s", o->atime);
    a[n++] = "-O"; a[n++] = compopt;
    a[n++] = "-O"; a[n++] = atimeopt;
    a[n++] = o->pool;

    /* split vdevs */
    static char vbuf[512]; set_str(vbuf, sizeof vbuf, o->vdevs);
    if (strncmp(vbuf, "raidz", 5) == 0) {
        char *colon = strchr(vbuf, ':');
        if (colon) {
            *colon = '\0';
            a[n++] = vbuf;               /* "raidz2" */
            char *p = colon + 1, *tok;
            while ((tok = strsep(&p, ",")) != NULL && n < (int)(8 + MAXVDEV))
                if (*tok) a[n++] = tok;
        }
    } else {
        a[n++] = vbuf;
    }
    return run_cmd("/zpool.so", a, n);
}

static const char *make_ds(const Opts *o, char *out, size_t cap) {
    if (o->ds[0]) set_str(out, cap, o->ds);
    else snprintf(out, cap, "%s/fs", o->pool);
    /* BSD-ZFS lacks large_blocks: cap recordsize at 128k so create succeeds */
    char rs[16]; set_str(rs, sizeof rs, o->recsize);
    if (!strcmp(o->impl, "bsd") &&
        (!strcmp(rs,"1M")||!strcmp(rs,"1m")||!strcmp(rs,"256k")||
         !strcmp(rs,"512k")||!strcmp(rs,"256K")||!strcmp(rs,"512K")))
        set_str(rs, sizeof rs, "128k");
    char rsopt[32]; snprintf(rsopt, sizeof rsopt, "recordsize=%s", rs);
    const char *a[] = {"zfs", "create", "-o", rsopt, out};
    run_cmd("/zfs.so", a, 5);
    return out;
}

/* ---- stats over fixed arrays ---- */
#define MAXREP 32
static int dcmp(const void *a, const void *b) {
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}
static double median(double *v, int n) {
    if (n == 0) return 0;
    qsort(v, n, sizeof(double), dcmp);
    return (n & 1) ? v[n/2] : (v[n/2-1] + v[n/2]) / 2.0;
}
static double stdev(double *v, int n) {
    if (n < 2) return 0;
    double m = 0; for (int i=0;i<n;i++) m += v[i]; m /= n;
    double s = 0; for (int i=0;i<n;i++) s += (v[i]-m)*(v[i]-m);
    return sqrt(s / (n - 1));
}
static void report_stat(const char *name, double *v, int n, const char *unit) {
    /* sort a copy for percentiles; median() sorts in place, fine here */
    double med = median(v, n), sd = stdev(v, n);
    printf("RESULT %s median=%.1f stdev=%.1f %s reps=[", name, med, sd, unit);
    for (int i=0;i<n;i++) printf("%s%.1f", i?",":"", v[i]);
    printf("]\n");
    fflush(stdout);
}
static long free_mb(void) {
    long p = sysconf(_SC_AVPHYS_PAGES), ps = sysconf(_SC_PAGESIZE);
    if (p < 0 || ps < 0) return 0;
    return (long)((long long)p * ps / (1024*1024));
}

/* ---- aligned buffer for O_DIRECT ---- */
static char *alloc_buf(size_t n, int aligned) {
    void *p = NULL;
    if (aligned) { if (posix_memalign(&p, 4096, n) != 0) return NULL; }
    else p = malloc(n);
    if (p) memset(p, 0xAB, n);
    return (char*)p;
}

/* ---- workloads ---- */
static double wl_seqwrite(const char *ds, const Opts *o, const char *fname) {
    char path[256]; snprintf(path, sizeof path, "/%s/%s", ds, fname);
    unlink(path);
    unsigned long size = o->size_mb * 1024UL * 1024UL;
    size_t BUF = 1024*1024;
    char *buf = alloc_buf(BUF, o->direct);
    int flags = O_CREAT | O_WRONLY | O_LARGEFILE;
#ifdef O_DIRECT
    if (o->direct) flags |= O_DIRECT;
#endif
    int f = open(path, flags, 0644);
    if (f < 0) { printf("seqwrite: open %s failed\n", path); free(buf); return 0; }
    double t0 = now_s();
    unsigned long left = size;
    while (left) { size_t n = left<BUF?left:BUF; ssize_t w=write(f,buf,n); if(w<=0)break; left-=w; }
    fsync(f); close(f);
    double t = now_s() - t0;
    free(buf);
    return t > 0 ? o->size_mb / t : 0;
}

static double wl_seqread(const char *ds, const Opts *o, const char *fname) {
    char path[256]; snprintf(path, sizeof path, "/%s/%s", ds, fname);
    unsigned long size = o->size_mb * 1024UL * 1024UL;
    size_t BUF = 1024*1024;
    char *buf = alloc_buf(BUF, o->direct);
    int flags = O_RDONLY | O_LARGEFILE;
#ifdef O_DIRECT
    if (o->direct) flags |= O_DIRECT;
#endif
    int f = open(path, flags);
    if (f < 0) { printf("seqread: open %s failed\n", path); free(buf); return 0; }
    double t0 = now_s();
    unsigned long left = size, total = 0;
    while (left) { size_t n=left<BUF?left:BUF; ssize_t r=read(f,buf,n); if(r<=0)break; total+=r; left-=r; }
    double t = now_s() - t0;
    close(f); free(buf);
    return t > 0 ? (total/(1024.0*1024.0)) / t : 0;
}

static void wl_randread(const char *ds, const Opts *o, const char *fname,
                        double *iops, double *p99us) {
    char path[256]; snprintf(path, sizeof path, "/%s/%s", ds, fname);
    unsigned long size = o->size_mb * 1024UL * 1024UL;
    size_t bs = o->bs;
    char *buf = alloc_buf(bs, o->direct);
    int flags = O_RDONLY | O_LARGEFILE;
#ifdef O_DIRECT
    if (o->direct) flags |= O_DIRECT;
#endif
    int f = open(path, flags);
    if (f < 0) { *iops=0; *p99us=0; free(buf); return; }
    unsigned long nblocks = size / bs;
    static double lat[400000]; unsigned long nl = 0;
    double tstart = now_s(), tend = tstart + (double)o->secs_run;
    unsigned long ops = 0;
    while (now_s() < tend) {
        off_t off = (off_t)(xrand() % nblocks) * bs;
        double a = now_s();
        ssize_t r = pread(f, buf, bs, off);
        double b = now_s();
        if (r <= 0) continue;
        if (nl < 400000) lat[nl++] = (b-a)*1e6;
        ops++;
    }
    double dur = now_s() - tstart;
    close(f); free(buf);
    *iops = dur > 0 ? ops / dur : 0;
    qsort(lat, nl, sizeof(double), dcmp);
    *p99us = nl ? lat[(size_t)(nl*0.99)] : 0;
}

static void wl_randwrite(const char *ds, const Opts *o, const char *fname,
                         double *iops, double *p99us) {
    char path[256]; snprintf(path, sizeof path, "/%s/%s", ds, fname);
    unsigned long size = o->size_mb * 1024UL * 1024UL;
    size_t bs = o->bs;
    char *buf = alloc_buf(bs, o->direct);
    int flags = O_WRONLY | O_LARGEFILE;
#ifdef O_DIRECT
    if (o->direct) flags |= O_DIRECT;
#endif
    int f = open(path, flags);
    if (f < 0) { *iops=0; *p99us=0; free(buf); return; }
    unsigned long nblocks = size / bs;
    static double lat[400000]; unsigned long nl = 0;
    double tstart = now_s(), tend = tstart + (double)o->secs_run;
    unsigned long ops = 0;
    while (now_s() < tend) {
        off_t off = (off_t)(xrand() % nblocks) * bs;
        double a = now_s();
        ssize_t w = pwrite(f, buf, bs, off);
        double b = now_s();
        if (w <= 0) continue;
        if (nl < 400000) lat[nl++] = (b-a)*1e6;
        ops++;
    }
    fsync(f);
    double dur = now_s() - tstart;
    close(f); free(buf);
    *iops = dur > 0 ? ops / dur : 0;
    qsort(lat, nl, sizeof(double), dcmp);
    *p99us = nl ? lat[(size_t)(nl*0.99)] : 0;
}

/* mmap seq read; MB/s + free-page delta (ARC<->pagecache bridge measure) */
static double wl_mmapread(const char *ds, const Opts *o, const char *fname,
                          long *free_delta_mb) {
    char path[256]; snprintf(path, sizeof path, "/%s/%s", ds, fname);
    struct stat st;
    if (stat(path, &st) != 0) { *free_delta_mb = 0; return 0; }
    size_t sz = st.st_size;
    int f = open(path, O_RDONLY | O_LARGEFILE);
    if (f < 0) { *free_delta_mb = 0; return 0; }
    long f_before = free_mb();
    void *m = mmap(NULL, sz, PROT_READ, MAP_SHARED, f, 0);
    if (m == MAP_FAILED) { close(f); *free_delta_mb = 0; return 0; }
    volatile uint64_t sink = 0;
    double t0 = now_s();
    const volatile char *p = (const volatile char*)m;
    for (size_t i = 0; i < sz; i += 4096) sink += p[i];
    double t = now_s() - t0;
    long f_after = free_mb();
    *free_delta_mb = f_before - f_after;
    munmap(m, sz); close(f);
    (void)sink;
    return t > 0 ? (sz/(1024.0*1024.0)) / t : 0;
}

static double wl_fsync(const char *ds, const Opts *o, const char *fname) {
    char path[256]; snprintf(path, sizeof path, "/%s/%s", ds, fname);
    unlink(path);
    int f = open(path, O_CREAT|O_WRONLY|O_LARGEFILE, 0644);
    if (f < 0) return 0;
    char buf[4096]; memset(buf, 0xCD, sizeof buf);
    double t0 = now_s(), tend = t0 + (double)o->secs_run;
    unsigned long n = 0;
    while (now_s() < tend) { ssize_t w=write(f, buf, sizeof buf); (void)w; fsync(f); n++; }
    double t = now_s() - t0;
    close(f);
    return t > 0 ? n / t : 0;
}

static double wl_meta(const char *ds, const Opts *o) {
    char dir[256]; snprintf(dir, sizeof dir, "/%s/meta", ds);
    mkdir(dir, 0755);
    unsigned long N = o->nfiles ? o->nfiles : 100000;
    double t0 = now_s();
    char nm[320];
    for (unsigned long i = 0; i < N; i++) {
        snprintf(nm, sizeof nm, "%s/f%lu", dir, i);
        int fd = open(nm, O_CREAT|O_WRONLY, 0644);
        if (fd >= 0) { ssize_t w=write(fd, "x", 1); (void)w; close(fd); }
        struct stat st; stat(nm, &st);
        unlink(nm);
    }
    double t = now_s() - t0;
    return t > 0 ? (3.0 * N) / t : 0;
}

static void usage(void) { printf("usage: /zfs-bench.so <workload> [k=v...]\n"); }

int main(int ac, char **av) {
    if (ac < 2) { usage(); return 1; }
    const char *wl = av[1];
    Opts o;
    memset(&o, 0, sizeof o);
    set_str(o.pool, sizeof o.pool, "bench");
    set_str(o.recsize, sizeof o.recsize, "1M");
    set_str(o.comp, sizeof o.comp, "off");
    set_str(o.atime, sizeof o.atime, "off");
    set_str(o.vdevs, sizeof o.vdevs, "/dev/vblk1");
    set_str(o.impl, sizeof o.impl, "unknown");
    o.size_mb = 6144; o.qd = 8; o.bs = 4096; o.secs_run = 30;
    o.reps = 3; o.nfiles = 100000; o.filesz_mb = 20480; o.direct = 0;
    parse_opts(&o, 2, ac, av);
    if (o.reps > MAXREP) o.reps = MAXREP;

    printf("=== zfs-bench workload=%s impl=%s pool=%s vdevs=%s recsize=%s comp=%s "
           "size_mb=%lu qd=%lu bs=%lu secs=%lu reps=%lu direct=%lu ===\n",
           wl, o.impl, o.pool, o.vdevs, o.recsize, o.comp, o.size_mb, o.qd,
           o.bs, o.secs_run, o.reps, o.direct);
    fflush(stdout);

    ensure_zfs_dev();

    if (!strcmp(wl, "info")) {
        const char *a[] = {"zpool", "version"};
        run_cmd("/zpool.so", a, 2);
        printf("RESULT free_mb=%ld\n", free_mb());
        return 0;
    }

    if (create_pool(&o) != 0) { printf("RESULT FAIL zpool create\n"); return 1; }
    char ds[128]; make_ds(&o, ds, sizeof ds);
    { const char *a[] = {"zpool", "status", o.pool}; run_cmd("/zpool.so", a, 3); }
    if (strcmp(o.impl, "bsd") != 0) {
        const char *a[] = {"zpool", "get", "ashift,size,free", o.pool};
        run_cmd("/zpool.so", a, 4);
    } else {
        const char *a[] = {"zpool", "get", "size,free", o.pool};
        run_cmd("/zpool.so", a, 4);
    }
    if (o.direct) { const char *a[]={"zfs","set","direct=always",ds}; run_cmd("/zfs.so", a, 4); }

    double v[MAXREP];
    if (!strcmp(wl, "seqwrite")) {
        for (unsigned long r=0;r<o.reps;r++) {
            /* distinct file per rep (f0,f1,...). Do NOT unlink+sync+sleep
             * between reps: freeing a multi-GB file triggers OpenZFS async
             * block-free that stalls the next write's txg on OSv (300s hangs).
             * 3 x size_mb fits easily on the 838G vdev. */
            char fn[16]; snprintf(fn, sizeof fn, "f%lu", r);
            v[r] = wl_seqwrite(ds,&o,fn);
        }
        report_stat("seqwrite_MBps", v, o.reps, "MB/s");
    } else if (!strcmp(wl, "seqread_cold")) {
        wl_seqwrite(ds,&o,"f0");
        { const char *a[]={"zfs","set","primarycache=none",ds}; run_cmd("/zfs.so", a, 4); }
        for (unsigned long r=0;r<o.reps;r++) v[r] = wl_seqread(ds,&o,"f0");
        report_stat("seqread_cold_MBps", v, o.reps, "MB/s");
        { const char *a[]={"zfs","set","primarycache=all",ds}; run_cmd("/zfs.so", a, 4); }
    } else if (!strcmp(wl, "seqread_warm")) {
        wl_seqwrite(ds,&o,"f0");
        wl_seqread(ds,&o,"f0");   /* prime ARC, discard */
        for (unsigned long r=0;r<o.reps;r++) v[r] = wl_seqread(ds,&o,"f0");
        report_stat("seqread_warm_MBps", v, o.reps, "MB/s");
    } else if (!strcmp(wl, "randread")) {
        wl_seqwrite(ds,&o,"f0");
        double vi[MAXREP], vp[MAXREP];
        for (unsigned long r=0;r<o.reps;r++) wl_randread(ds,&o,"f0",&vi[r],&vp[r]);
        report_stat("randread_IOPS", vi, o.reps, "iops");
        report_stat("randread_p99", vp, o.reps, "us");
    } else if (!strcmp(wl, "randwrite")) {
        wl_seqwrite(ds,&o,"f0");
        double vi[MAXREP], vp[MAXREP];
        for (unsigned long r=0;r<o.reps;r++) wl_randwrite(ds,&o,"f0",&vi[r],&vp[r]);
        report_stat("randwrite_IOPS", vi, o.reps, "iops");
        report_stat("randwrite_p99", vp, o.reps, "us");
    } else if (!strcmp(wl, "mmapread")) {
        wl_seqwrite(ds,&o,"f0");
        double fd[MAXREP];
        for (unsigned long r=0;r<o.reps;r++) { long d; v[r]=wl_mmapread(ds,&o,"f0",&d); fd[r]=(double)d; }
        report_stat("mmapread_MBps", v, o.reps, "MB/s");
        report_stat("mmapread_freedelta", fd, o.reps, "MB");
    } else if (!strcmp(wl, "fsync")) {
        for (unsigned long r=0;r<o.reps;r++) v[r] = wl_fsync(ds,&o,"zil");
        report_stat("fsync_persec", v, o.reps, "fsync/s");
    } else if (!strcmp(wl, "meta")) {
        for (unsigned long r=0;r<o.reps;r++) v[r] = wl_meta(ds,&o);
        report_stat("meta_opspers", v, o.reps, "ops/s");
    } else if (!strcmp(wl, "compress")) {
        char path[256]; snprintf(path,sizeof path,"/%s/c0",ds); unlink(path);
        unsigned long size = o.size_mb * 1024UL * 1024UL;
        size_t BUF = 1024*1024;
        char *buf = alloc_buf(BUF, 0);
        memset(buf, 0, BUF);
        for (size_t i=0;i<BUF;i+=512) buf[i] = (char)(i & 0xff); /* lz4-friendly */
        int f = open(path, O_CREAT|O_WRONLY|O_LARGEFILE, 0644);
        double t0 = now_s();
        unsigned long left = size;
        while (left) { size_t n=left<BUF?left:BUF; ssize_t w=write(f,buf,n); if(w<=0)break; left-=n; }
        fsync(f); close(f);
        double t = now_s() - t0;
        free(buf);
        printf("RESULT compress_%s_MBps %.1f MB/s\n", o.comp, t>0?o.size_mb/t:0);
        const char *a[] = {"zfs","get","compressratio,used,logicalused",ds};
        run_cmd("/zfs.so", a, 3);
    } else if (!strcmp(wl, "scrub")) {
        wl_seqwrite(ds,&o,"f0");
        { const char *a[]={"zpool","scrub",o.pool}; run_cmd("/zpool.so", a, 3); }
        double t0 = now_s();
        /* size-proportional settle then read final status */
        int settle = (int)(o.size_mb/500 + 3);
        for (int i=0;i<settle && i<600;i++) sleep(1);
        double t = now_s() - t0;
        { const char *a[]={"zpool","status",o.pool}; run_cmd("/zpool.so", a, 3); }
        printf("RESULT scrub_MBps ~%.1f MB/s (scanned ~%lu MB in ~%.1fs; see status)\n",
               t>0?o.size_mb/t:0, o.size_mb, t);
    } else if (!strcmp(wl, "odirect")) {
        double w = wl_seqwrite(ds,&o,"d0");
        double rc = wl_seqread(ds,&o,"d0");
        printf("RESULT odirect_write_MBps %.1f MB/s\n", w);
        printf("RESULT odirect_read_MBps %.1f MB/s\n", rc);
        const char *a[] = {"zpool","get","free",o.pool}; run_cmd("/zpool.so", a, 4);
    } else {
        printf("unknown workload %s\n", wl);
        usage();
        return 1;
    }

    printf("=== zfs-bench done (%s) ===\n", wl);
    fflush(stdout);
    return 0;
}
