/*
 * Copyright (C) 2026 OSv Authors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * Filesystem benchmark for OSv.
 *
 * Measures sequential read/write throughput, random 4KB I/O, metadata
 * operations, and mmap read across all OSv filesystems.
 *
 * Usage (as OSv execute argument):
 *   tests/tst-fs-bench.so [--dir /path] [--size-mb N] [--nfiles N] [--prebuilt]
 *
 * --dir PATH    working directory (default: /bench)
 * --size-mb N   file size in MB for seq/random tests (default: 32)
 * --nfiles N    number of files for metadata test (default: 200)
 * --prebuilt    files already exist in --dir; skip creation, only read tests
 *
 * Pre-built layout expected by --prebuilt:
 *   PATH/seq_4096.bin          seq_size_mb MB of data (4K alignment)
 *   PATH/seq_131072.bin        seq_size_mb MB of data (128K alignment)
 *   PATH/rand.bin              seq_size_mb MB of random data
 *   PATH/meta/f0000.bin ...    nfiles x 4KB files
 *
 * Output lines of the form are parsed by run-fs-benchmarks.sh:
 *   BENCH: name = value unit
 */

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

using hrc   = std::chrono::high_resolution_clock;
using fsec  = std::chrono::duration<double>;

/* -------------------------------------------------------------------------- */
static std::string g_dir      = "/bench";
static size_t      g_size_mb  = 32;
static int         g_nfiles   = 200;
static bool        g_prebuilt = false;

static void report(const char *name, double val, const char *unit)
{
    printf("BENCH: %-42s = %10.2f %s\n", name, val, unit);
    fflush(stdout);
}
static void skip(const char *name)
{
    printf("BENCH: %-42s =       SKIP\n", name);
    fflush(stdout);
}

/* --------------------------------------------------------------------------
 * Sequential I/O
 * -------------------------------------------------------------------------- */
static double seq_write(const char *dir, size_t file_sz, size_t io_sz)
{
    std::vector<char> buf(io_sz, (char)0xAB);
    char path[512];
    snprintf(path, sizeof(path), "%s/seq_%zu.bin", dir, io_sz);
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return -1;
    auto t0 = hrc::now();
    for (size_t done = 0; done < file_sz;) {
        ssize_t n = write(fd, buf.data(), buf.size());
        if (n <= 0) break;
        done += (size_t)n;
    }
    fsync(fd);
    close(fd);
    return (double)file_sz / (1024.0 * 1024.0) /
           fsec(hrc::now() - t0).count();
}

static double seq_read(const char *dir, size_t file_sz, size_t io_sz)
{
    std::vector<char> buf(io_sz);
    char path[512];
    snprintf(path, sizeof(path), "%s/seq_%zu.bin", dir, io_sz);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    auto t0 = hrc::now();
    for (size_t done = 0; done < file_sz;) {
        ssize_t n = read(fd, buf.data(), buf.size());
        if (n <= 0) break;
        done += (size_t)n;
    }
    close(fd);
    return (double)file_sz / (1024.0 * 1024.0) /
           fsec(hrc::now() - t0).count();
}

/* --------------------------------------------------------------------------
 * Random 4KB I/O
 * -------------------------------------------------------------------------- */
static double rand_read(const char *dir, size_t file_sz, int nops)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/rand.bin", dir);
    char buf[4096];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    const size_t n_pages = file_sz / 4096;
    srand(42);
    auto t0 = hrc::now();
    for (int i = 0; i < nops; i++) {
        off_t off = (off_t)(rand() % (int)n_pages) * 4096;
        pread(fd, buf, 4096, off);
    }
    close(fd);
    return nops / fsec(hrc::now() - t0).count();
}

static double rand_write(const char *dir, size_t file_sz, int nops)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/rand.bin", dir);
    char buf[4096];
    memset(buf, 0xCD, sizeof(buf));
    int fd = open(path, O_RDWR);
    if (fd < 0) return -1;
    const size_t n_pages = file_sz / 4096;
    srand(42);
    auto t0 = hrc::now();
    for (int i = 0; i < nops; i++) {
        off_t off = (off_t)(rand() % (int)n_pages) * 4096;
        pwrite(fd, buf, 4096, off);
    }
    fsync(fd);
    close(fd);
    return nops / fsec(hrc::now() - t0).count();
}

/* --------------------------------------------------------------------------
 * Metadata operations
 * -------------------------------------------------------------------------- */
static double meta_create(const char *dir, int n)
{
    char buf[4096] = {};
    char path[512];
    auto t0 = hrc::now();
    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof(path), "%s/meta/f%04d.bin", dir, i);
        int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd >= 0) { write(fd, buf, sizeof(buf)); close(fd); }
    }
    return n / fsec(hrc::now() - t0).count();
}

static double meta_stat(const char *dir, int n)
{
    char path[512];
    struct stat st;
    auto t0 = hrc::now();
    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof(path), "%s/meta/f%04d.bin", dir, i);
        stat(path, &st);
    }
    return n / fsec(hrc::now() - t0).count();
}

static double meta_readdir(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/meta", dir);
    int count = 0;
    auto t0 = hrc::now();
    DIR *d = opendir(path);
    if (d) { while (readdir(d)) { count++; } closedir(d); }
    return count / fsec(hrc::now() - t0).count();
}

static double meta_unlink(const char *dir, int n)
{
    char path[512];
    auto t0 = hrc::now();
    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof(path), "%s/meta/f%04d.bin", dir, i);
        unlink(path);
    }
    return n / fsec(hrc::now() - t0).count();
}

/* --------------------------------------------------------------------------
 * mmap sequential scan
 * -------------------------------------------------------------------------- */
static double mmap_read(const char *dir, size_t file_sz)
{
    /* prefer the 128K-aligned sequential file */
    char path[512];
    snprintf(path, sizeof(path), "%s/seq_131072.bin", dir);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        snprintf(path, sizeof(path), "%s/seq_4096.bin", dir);
        fd = open(path, O_RDONLY);
    }
    if (fd < 0) return -1;

    void *p = mmap(nullptr, file_sz, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return -1;

    volatile uint64_t sum = 0;
    const uint64_t *q = (const uint64_t *)p;
    auto t0 = hrc::now();
    for (size_t i = 0; i < file_sz / sizeof(uint64_t); i++)
        sum += q[i];
    double s = fsec(hrc::now() - t0).count();
    munmap(p, file_sz);
    (void)sum;
    return (double)file_sz / (1024.0 * 1024.0) / s;
}

/* --------------------------------------------------------------------------
 * Utilities
 * -------------------------------------------------------------------------- */
static bool is_writable(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/.wtest", dir);
    int fd = open(path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) return false;
    close(fd);
    unlink(path);
    return true;
}

static void mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

static void cleanup(const char *dir, int nfiles)
{
    char path[512];
    for (const char *sf : {"seq_4096.bin", "seq_131072.bin", "rand.bin"}) {
        snprintf(path, sizeof(path), "%s/%s", dir, sf);
        unlink(path);
    }
    for (int i = 0; i < nfiles; i++) {
        snprintf(path, sizeof(path), "%s/meta/f%04d.bin", dir, i);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/meta", dir);
    rmdir(path);
}

static void print_statvfs(const char *dir)
{
    struct statvfs sv;
    if (statvfs(dir, &sv) == 0) {
        double free_mb = (double)sv.f_bavail * sv.f_bsize / (1024.0 * 1024.0);
        double total_mb = (double)sv.f_blocks * sv.f_frsize / (1024.0 * 1024.0);
        printf("  statvfs: %.0f MB free / %.0f MB total  (block=%lu)\n",
               free_mb, total_mb, sv.f_bsize);
    }
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc)
            g_dir = argv[++i];
        else if (!strcmp(argv[i], "--size-mb") && i + 1 < argc)
            g_size_mb = (size_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--nfiles") && i + 1 < argc)
            g_nfiles = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--prebuilt"))
            g_prebuilt = true;
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return 1;
        }
    }

    const char *dir     = g_dir.c_str();
    const size_t fsz    = g_size_mb * 1024UL * 1024UL;
    const int    nrand  = 500;  /* random I/O ops — conservative for laptop */
    double v;

    printf("=== OSv filesystem benchmark ===\n");
    printf("  dir:      %s\n", dir);
    printf("  file:     %zu MB\n", g_size_mb);
    printf("  nfiles:   %d\n", g_nfiles);
    printf("  prebuilt: %s\n", g_prebuilt ? "yes" : "no");
    print_statvfs(dir);
    printf("\n");

    bool writable = !g_prebuilt && is_writable(dir);

    if (!writable && !g_prebuilt) {
        fprintf(stderr, "ERROR: %s is not writable and --prebuilt not set\n", dir);
        return 1;
    }

    /* --- Create working directories and test files --- */
    if (writable) {
        mkdir_p(dir);
        char mdir[512]; snprintf(mdir, sizeof(mdir), "%s/meta", dir);
        mkdir_p(mdir);

        printf("--- Creating test data ---\n");

        /* Sequential files */
        printf("  writing seq_4096.bin (%zu MB)...\n", g_size_mb); fflush(stdout);
        seq_write(dir, fsz, 4096);
        printf("  writing seq_131072.bin (%zu MB)...\n", g_size_mb); fflush(stdout);
        seq_write(dir, fsz, 131072);

        /* Random-access file */
        printf("  writing rand.bin (%zu MB)...\n", g_size_mb); fflush(stdout);
        {
            char path[512];
            snprintf(path, sizeof(path), "%s/rand.bin", dir);
            int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (fd >= 0) {
                std::vector<char> buf(65536, (char)0xDE);
                for (size_t done = 0; done < fsz;) {
                    ssize_t n = write(fd, buf.data(), buf.size());
                    if (n <= 0) break;
                    done += (size_t)n;
                }
                fsync(fd); close(fd);
            }
        }

        /* Metadata files */
        printf("  creating %d metadata files...\n", g_nfiles); fflush(stdout);
        meta_create(dir, g_nfiles);
        printf("\n");
    }

    /* ------------------------------------------------------------------ */
    printf("--- Sequential write (MB/s) ---\n");
    if (writable) {
        v = seq_write(dir, fsz, 4096);   report("seq_write_4k",   v, "MB/s");
        v = seq_write(dir, fsz, 131072); report("seq_write_128k", v, "MB/s");
    } else {
        skip("seq_write_4k");
        skip("seq_write_128k");
    }

    printf("--- Sequential read (MB/s) ---\n");
    v = seq_read(dir, fsz, 4096);   report("seq_read_4k",   v, "MB/s");
    v = seq_read(dir, fsz, 131072); report("seq_read_128k", v, "MB/s");

    printf("--- Random 4KB I/O (IOPS, %d ops) ---\n", nrand);
    v = rand_read(dir, fsz, nrand);
    report("rand_read_4k", v, "IOPS");
    if (writable) {
        v = rand_write(dir, fsz, nrand);
        report("rand_write_4k", v, "IOPS");
    } else {
        skip("rand_write_4k");
    }

    printf("--- Metadata ops/s (%d files) ---\n", g_nfiles);
    if (writable) {
        /* files were pre-created during setup; re-create to time it */
        cleanup(dir, g_nfiles);
        char mdir[512]; snprintf(mdir, sizeof(mdir), "%s/meta", dir);
        mkdir_p(mdir);
        v = meta_create(dir, g_nfiles); report("meta_create", v, "ops/s");
    } else {
        skip("meta_create");
    }
    v = meta_stat(dir, g_nfiles);     report("meta_stat",   v, "ops/s");
    v = meta_readdir(dir);            report("meta_readdir", v, "entries/s");
    if (writable) {
        v = meta_unlink(dir, g_nfiles); report("meta_unlink", v, "ops/s");
    } else {
        skip("meta_unlink");
    }

    printf("--- mmap sequential scan (MB/s) ---\n");
    v = mmap_read(dir, fsz);
    if (v > 0) report("mmap_seq_read", v, "MB/s");
    else        skip("mmap_seq_read");

    /* ------------------------------------------------------------------ */
    printf("\n=== Done ===\n");

    /* Cleanup (writable only; prebuilt files are owned by image builder) */
    if (writable) {
        printf("Cleaning up test files...\n");
        cleanup(dir, g_nfiles);
        /* Only rmdir the bench dir if we created it */
        rmdir(dir);
    }

    return 0;
}
