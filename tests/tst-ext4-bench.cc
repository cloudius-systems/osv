/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// ext4 (libext/lwext4) read/write throughput micro-benchmark for the D2.3 A/B.
//
// Measures sequential write, sequential read (cold + warm), and random read
// throughput against a file on a mounted filesystem, reporting MB/s so the
// ext4 path can be compared to raw virtio-blk and to Linux ext4 on the same
// storage. It is filesystem-agnostic: point --dir at an ext mount for the ext4
// numbers, or at a zfs/rofs mount for a cross-fs comparison.
//
// Boot (ext4 second disk mounted at /data):
//   scripts/run.py --second-disk-image ./ext_images/ext4.img
//     --execute='--mount-fs=ext,/dev/vblk1,/data tests/tst-ext4-bench.so /data'

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <chrono>
#include <vector>
#include <random>

using clk = std::chrono::steady_clock;

static double secs(clk::time_point a, clk::time_point b)
{
    return std::chrono::duration<double>(b - a).count();
}

// Write `total` bytes to `path` in `bs`-sized chunks; return MB/s.
static double bench_write(const char *path, size_t total, size_t bs, bool do_fsync)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open(write)"); return -1; }
    std::vector<char> buf(bs);

    auto t0 = clk::now();
    size_t written = 0;
    while (written < total) {
        // Absolute-offset pattern so a sequential read can verify integrity
        // regardless of chunk size / read-ahead window boundaries.
        for (size_t i = 0; i < bs; i++)
            buf[i] = (char)(((written + i) * 7 + 3) & 0xff);
        ssize_t n = write(fd, buf.data(), bs);
        if (n != (ssize_t)bs) { perror("write"); close(fd); return -1; }
        written += n;
    }
    if (do_fsync && fsync(fd) != 0) { perror("fsync"); }
    auto t1 = clk::now();
    close(fd);
    return (total / (1024.0 * 1024.0)) / secs(t0, t1);
}

// Sequential read of `total` bytes in `bs` chunks; return MB/s. If verify is
// set, checks each byte matches the write pattern (i*7+3)&0xff, proving the
// read path (incl. read-ahead) returns correct data.
static double bench_read_seq(const char *path, size_t total, size_t bs, bool verify = false)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open(read)"); return -1; }
    std::vector<char> buf(bs);
    auto t0 = clk::now();
    size_t rd = 0;
    while (rd < total) {
        ssize_t n = read(fd, buf.data(), bs);
        if (n <= 0) break;
        if (verify) {
            for (ssize_t i = 0; i < n; i++) {
                char expect = (char)(((rd + i) * 7 + 3) & 0xff);
                if (buf[i] != expect) {
                    fprintf(stderr, "VERIFY FAIL at byte %zu: got %d want %d\n",
                            rd + i, (unsigned char)buf[i], (unsigned char)expect);
                    close(fd);
                    return -2;
                }
            }
        }
        rd += n;
    }
    auto t1 = clk::now();
    close(fd);
    return (rd / (1024.0 * 1024.0)) / secs(t0, t1);
}

// Random read: `count` reads of `bs` at random block offsets; return MB/s.
static double bench_read_rand(const char *path, size_t file_size, size_t bs, int count)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open(rand)"); return -1; }
    std::vector<char> buf(bs);
    std::mt19937_64 rng(12345);
    size_t nblocks = file_size / bs;
    std::uniform_int_distribution<size_t> dist(0, nblocks ? nblocks - 1 : 0);

    auto t0 = clk::now();
    size_t rd = 0;
    for (int i = 0; i < count; i++) {
        off_t off = (off_t)dist(rng) * bs;
        ssize_t n = pread(fd, buf.data(), bs, off);
        if (n <= 0) break;
        rd += n;
    }
    auto t1 = clk::now();
    close(fd);
    return (rd / (1024.0 * 1024.0)) / secs(t0, t1);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "/data";
    // Sizes: modest by default so it runs on small images; override via argv.
    size_t total = (argc > 2) ? strtoull(argv[2], nullptr, 0) : (64UL << 20); // 64 MiB
    size_t bs = (argc > 3) ? strtoull(argv[3], nullptr, 0) : (128UL << 10);   // 128 KiB

    char path[512];
    snprintf(path, sizeof(path), "%s/bench.dat", dir);

    fprintf(stderr, "ext4-bench: dir=%s total=%zuMiB bs=%zuKiB\n",
            dir, total >> 20, bs >> 10);

    double w   = bench_write(path, total, bs, /*fsync=*/true);
    double rs  = bench_read_seq(path, total, bs, /*verify=*/true); // warm + integrity check
    double rr  = bench_read_rand(path, total, 4096, 4096); // 4K random, 4096 ops

    fprintf(stderr, "RESULT seq_write_fsync %.1f MB/s\n", w);
    fprintf(stderr, "RESULT seq_read        %.1f MB/s\n", rs);
    fprintf(stderr, "RESULT rand_read_4k    %.1f MB/s\n", rr);

    unlink(path);
    fprintf(stderr, "ext4-bench done\n");
    return (w > 0 && rs > 0 && rr > 0) ? 0 : 1;
}
