/*
 * Copyright (C) 2026 OSv Authors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * tst-crucible-bench: real benchmark harness for the OSv Crucible
 * block driver.  Measures throughput and latency at several block
 * sizes and access patterns against a 3-replica volume.
 *
 * Workloads:
 *   - Sequential write + sync at 4 KiB / 64 KiB / 1 MiB
 *   - Sequential read at the same block sizes
 *   - Random read at 4 KiB
 *   - Random write at 4 KiB (with fsync per N writes)
 *
 * Reports per workload: throughput (MB/s, IOPS) and latency
 * percentiles (p50, p95, p99) in microseconds.  All numbers are over
 * direct /dev/crucible0 access, so the ZFS pageca­che / record
 * boundary effects are out of the picture; this isolates the Crucible
 * upstairs/downstairs path.
 *
 * Run:
 *   ./scripts/run.py -k --arch=x86_64 -m 1024 -c2 \
 *     --crucible=HOST:P1,HOST:P2,HOST:P3 --crucible-uuid=... \
 *     -e "tests/tst-crucible-bench.so"
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

constexpr const char *DEV = "/dev/crucible0";

/*
 * Per-IO sample sizes.  Tuned for completion in well under a minute on
 * a localhost 3-replica cluster while still giving statistically
 * meaningful samples for latency percentiles.
 */
constexpr size_t SEQ_BYTES_PER_PASS = 64ull * 1024 * 1024;  /* 64 MiB */
constexpr size_t RAND_OPS           = 4096;

struct stats {
    double min_us = 0;
    double p50_us = 0;
    double p95_us = 0;
    double p99_us = 0;
    double max_us = 0;
    double mean_us = 0;
    double total_secs = 0;
};

static stats summarize(std::vector<double> &lat_us)
{
    stats s;
    if (lat_us.empty()) return s;
    std::sort(lat_us.begin(), lat_us.end());
    auto pick = [&](double pct) {
        size_t i = (size_t)(pct * (lat_us.size() - 1));
        return lat_us[i];
    };
    s.min_us = lat_us.front();
    s.max_us = lat_us.back();
    s.p50_us = pick(0.50);
    s.p95_us = pick(0.95);
    s.p99_us = pick(0.99);
    double sum = 0;
    for (double v : lat_us) sum += v;
    s.mean_us = sum / lat_us.size();
    s.total_secs = sum / 1e6;
    return s;
}

static void print_header(const char *title)
{
    printf("\n=== %s ===\n", title);
    printf("  %-22s %10s %10s %10s %10s %10s %10s\n",
           "workload", "MB/s", "IOPS", "p50_us", "p95_us", "p99_us", "max_us");
}

static void print_row(const char *name, size_t bytes_total, size_t ops,
                      const stats &s, double wall_secs)
{
    double mbps = (bytes_total / (1024.0 * 1024.0)) / wall_secs;
    double iops = ops / wall_secs;
    printf("  %-22s %10.1f %10.0f %10.1f %10.1f %10.1f %10.1f\n",
           name, mbps, iops, s.p50_us, s.p95_us, s.p99_us, s.max_us);
}

/*
 * Sequential write workload.  Allocates a deterministic-pattern
 * buffer of `block_size` bytes and writes it `ops` times sequentially
 * starting at offset 0.  Issues a single fsync at the end so we
 * measure the queue-depth-1 latency of each pwrite, plus the cost of
 * one final flush.
 */
static bool seq_write(int fd, size_t block_size, size_t bytes_total,
                      const char *label, void *aligned_buf)
{
    size_t ops = bytes_total / block_size;
    std::vector<double> lat;
    lat.reserve(ops);
    auto wall0 = std::chrono::high_resolution_clock::now();
    uint64_t off = 0;
    for (size_t i = 0; i < ops; i++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        ssize_t n = pwrite(fd, aligned_buf, block_size, off);
        auto t1 = std::chrono::high_resolution_clock::now();
        if (n != (ssize_t)block_size) {
            printf("  pwrite(%zu @ %lu) = %zd (errno=%d)\n",
                   block_size, off, n, errno);
            return false;
        }
        lat.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        off += block_size;
    }
    if (fsync(fd) != 0) {
        printf("  fsync after %s failed: errno=%d\n", label, errno);
        return false;
    }
    auto wall1 = std::chrono::high_resolution_clock::now();
    double wall = std::chrono::duration<double>(wall1 - wall0).count();
    auto s = summarize(lat);
    print_row(label, bytes_total, ops, s, wall);
    return true;
}

static bool seq_read(int fd, size_t block_size, size_t bytes_total,
                     const char *label, void *aligned_buf)
{
    size_t ops = bytes_total / block_size;
    std::vector<double> lat;
    lat.reserve(ops);
    auto wall0 = std::chrono::high_resolution_clock::now();
    uint64_t off = 0;
    for (size_t i = 0; i < ops; i++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        ssize_t n = pread(fd, aligned_buf, block_size, off);
        auto t1 = std::chrono::high_resolution_clock::now();
        if (n != (ssize_t)block_size) {
            printf("  pread(%zu @ %lu) = %zd (errno=%d)\n",
                   block_size, off, n, errno);
            return false;
        }
        lat.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        off += block_size;
    }
    auto wall1 = std::chrono::high_resolution_clock::now();
    double wall = std::chrono::duration<double>(wall1 - wall0).count();
    auto s = summarize(lat);
    print_row(label, bytes_total, ops, s, wall);
    return true;
}

/*
 * Random reads: pick a block-aligned offset uniformly within the
 * device extent and read one block.  Seeds the PRNG with a constant
 * so runs are repeatable.
 */
static bool rand_read(int fd, uint64_t dev_bytes, size_t block_size,
                      size_t ops, const char *label, void *aligned_buf)
{
    uint64_t blocks = dev_bytes / block_size;
    std::mt19937_64 rng(0xC1EAB1ECULL);
    std::uniform_int_distribution<uint64_t> dist(0, blocks - 1);
    std::vector<double> lat;
    lat.reserve(ops);
    auto wall0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < ops; i++) {
        uint64_t off = dist(rng) * block_size;
        auto t0 = std::chrono::high_resolution_clock::now();
        ssize_t n = pread(fd, aligned_buf, block_size, off);
        auto t1 = std::chrono::high_resolution_clock::now();
        if (n != (ssize_t)block_size) {
            printf("  pread(%zu @ %lu) = %zd (errno=%d)\n",
                   block_size, off, n, errno);
            return false;
        }
        lat.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    auto wall1 = std::chrono::high_resolution_clock::now();
    double wall = std::chrono::duration<double>(wall1 - wall0).count();
    auto s = summarize(lat);
    print_row(label, ops * block_size, ops, s, wall);
    return true;
}

/*
 * Random writes with periodic fsync: every fsync_every ops we issue
 * an fsync to drive the Flush path.  Without fsync, dirty buffers
 * never reach the wire and the test is meaningless for Crucible.
 */
static bool rand_write(int fd, uint64_t dev_bytes, size_t block_size,
                       size_t ops, size_t fsync_every,
                       const char *label, void *aligned_buf)
{
    uint64_t blocks = dev_bytes / block_size;
    std::mt19937_64 rng(0xCAFE4321ULL);
    std::uniform_int_distribution<uint64_t> dist(0, blocks - 1);
    std::vector<double> lat;
    lat.reserve(ops);
    auto wall0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < ops; i++) {
        uint64_t off = dist(rng) * block_size;
        auto t0 = std::chrono::high_resolution_clock::now();
        ssize_t n = pwrite(fd, aligned_buf, block_size, off);
        auto t1 = std::chrono::high_resolution_clock::now();
        if (n != (ssize_t)block_size) {
            printf("  pwrite(%zu @ %lu) = %zd (errno=%d)\n",
                   block_size, off, n, errno);
            return false;
        }
        lat.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        if ((i + 1) % fsync_every == 0) {
            if (fsync(fd) != 0) {
                printf("  fsync mid-stream failed: errno=%d\n", errno);
                return false;
            }
        }
    }
    if (fsync(fd) != 0) {
        printf("  final fsync failed: errno=%d\n", errno);
        return false;
    }
    auto wall1 = std::chrono::high_resolution_clock::now();
    double wall = std::chrono::duration<double>(wall1 - wall0).count();
    auto s = summarize(lat);
    print_row(label, ops * block_size, ops, s, wall);
    return true;
}

} /* anonymous namespace */

int main()
{
    printf("=== tst-crucible-bench ===\n\n");

    struct stat st;
    if (stat(DEV, &st) != 0) {
        printf("SKIP: %s not present (boot without --crucible= ?)\n", DEV);
        return 0;
    }
    int fd = open(DEV, O_RDWR);
    if (fd < 0) {
        printf("FAIL: open(%s): %s\n", DEV, strerror(errno));
        return 1;
    }

    uint64_t dev_bytes = 0;
    if (ioctl(fd, BLKGETSIZE64, &dev_bytes) != 0) {
        printf("FAIL: BLKGETSIZE64: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("  device: %s, size: %.2f GiB\n",
           DEV, dev_bytes / (1024.0 * 1024.0 * 1024.0));

    /* Avoid touching the whole device for sequential workloads; they
     * write linearly from offset 0.  Cap to leave room for random
     * workloads and to keep wall time reasonable. */
    if (SEQ_BYTES_PER_PASS > dev_bytes / 2) {
        printf("FAIL: device too small for benchmark (need %zu bytes)\n",
               2 * SEQ_BYTES_PER_PASS);
        close(fd);
        return 1;
    }

    /* posix_memalign keeps the buffer 4 KiB-aligned in case the upper
     * layer (in this case Crucible directly, but kept for portability)
     * requires direct-IO alignment. */
    void *buf = nullptr;
    constexpr size_t MAX_BLOCK = 1ull * 1024 * 1024;
    if (posix_memalign(&buf, 4096, MAX_BLOCK) != 0) {
        printf("FAIL: posix_memalign\n");
        close(fd);
        return 1;
    }
    /* Deterministic non-zero pattern. */
    auto *p = static_cast<uint8_t *>(buf);
    for (size_t i = 0; i < MAX_BLOCK; i++) {
        p[i] = static_cast<uint8_t>((i * 31u + (i >> 11)) ^ 0xA7);
    }

    print_header("sequential writes (then fsync)");
    seq_write(fd,    4 * 1024,        SEQ_BYTES_PER_PASS, "seq write 4K   ", buf);
    seq_write(fd,   64 * 1024,        SEQ_BYTES_PER_PASS, "seq write 64K  ", buf);
    seq_write(fd, 1024 * 1024,        SEQ_BYTES_PER_PASS, "seq write 1M   ", buf);

    print_header("sequential reads");
    seq_read (fd,    4 * 1024,        SEQ_BYTES_PER_PASS, "seq read 4K    ", buf);
    seq_read (fd,   64 * 1024,        SEQ_BYTES_PER_PASS, "seq read 64K   ", buf);
    seq_read (fd, 1024 * 1024,        SEQ_BYTES_PER_PASS, "seq read 1M    ", buf);

    print_header("random reads");
    rand_read(fd, dev_bytes, 4 * 1024,   RAND_OPS, "rand read 4K   ", buf);
    rand_read(fd, dev_bytes, 64 * 1024,  RAND_OPS, "rand read 64K  ", buf);

    print_header("random writes (fsync every 64 ops)");
    rand_write(fd, dev_bytes, 4 * 1024,  RAND_OPS, 64, "rand write 4K  ", buf);

    free(buf);
    close(fd);

    printf("\n=== bench complete ===\n");
    return 0;
}
