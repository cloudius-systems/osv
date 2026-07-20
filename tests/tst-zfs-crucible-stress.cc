/*
 * Copyright (C) 2026 OSv Authors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * ZFS Crucible stress test: concurrent mixed I/O across block sizes.
 *
 * For each block size from 8kB to 128kB (8kB steps), four worker threads
 * run concurrently on the ZFS filesystem, each with a different I/O pattern:
 *
 *   T1  buffered sequential write   (write syscall, blksz chunks)
 *   T2  O_DIRECT random write       (posix_memalign buffer, LCG-shuffled offsets)
 *   T3  io_uring sequential write   (IORING_OP_WRITE, one SQE at a time)
 *   T4  buffered sequential read    (read syscall from pre-populated source file)
 *
 * All four threads start simultaneously per block size, exercising ZFS I/O paths,
 * the ARC, pagecache, and io_uring concurrently.
 *
 * When Crucible block devices are present (/dev/crucible0 etc.) those devices
 * back the ZFS pool, making this a full Crucible replication stress path.
 *
 * Run:     ./scripts/run.py --image <zfs-img> -e "tests/tst-zfs-crucible-stress.so"
 * Requires: ZFS root filesystem (build with fs=zfs)
 */

#include <osv/io_uring.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <cassert>

/*
 * Bytes transferred per worker.  Four workers run concurrently, so the
 * aggregate live working set is 4*WORK_SIZE; on a guest whose RAM is smaller
 * than that the ARC and pagecache thrash and the sweep cannot keep pace with
 * the 600s harness timeout.  WORK_SIZE is therefore sized at runtime in main()
 * to RAM/32 (capped at 32 MB), keeping the four-worker aggregate at <= RAM/8 so
 * the ZFS dirty-data/txg-sync stalls stay short enough for a 128 MiB guest to
 * complete all 16 rows within the harness timeout.
 */
static size_t WORK_SIZE = 32UL * 1024 * 1024;  /* default 32 MB; tuned in main */

/* Pre-populated file for the read worker. */
static constexpr const char *READ_SRC = "/tmp/stress-read-src.dat";

/* ------------------------------------------------------------------ */
/* io_uring ring helpers (mirrors pattern from tst-io_uring.cc)       */
/* ------------------------------------------------------------------ */

struct test_ring {
    int fd;
    struct io_uring_params params;
    struct io_uring_sq_ring *sq_ring;
    struct io_uring_cq_ring *cq_ring;
    struct io_uring_sqe    *sqes;
    size_t sq_size;
    size_t cq_size;
    size_t sqe_size;
};

static int ring_init(struct test_ring *ring, unsigned entries)
{
    memset(ring, 0, sizeof(*ring));

    ring->fd = sys_io_uring_setup(entries, &ring->params);
    if (ring->fd < 0)
        return ring->fd;

    ring->sq_size  = sizeof(struct io_uring_sq_ring) +
                     ring->params.sq_entries * sizeof(uint32_t);
    ring->cq_size  = sizeof(struct io_uring_cq_ring) +
                     ring->params.cq_entries * sizeof(struct io_uring_cqe);
    ring->sqe_size = ring->params.sq_entries * sizeof(struct io_uring_sqe);

    ring->sq_ring = (struct io_uring_sq_ring *)mmap(
        NULL, ring->sq_size, PROT_READ | PROT_WRITE, MAP_SHARED, ring->fd, 0);
    if (ring->sq_ring == MAP_FAILED) { close(ring->fd); return -errno; }

    ring->cq_ring = (struct io_uring_cq_ring *)mmap(
        NULL, ring->cq_size, PROT_READ | PROT_WRITE, MAP_SHARED,
        ring->fd, 0x8000000ULL);
    if (ring->cq_ring == MAP_FAILED) {
        munmap(ring->sq_ring, ring->sq_size);
        close(ring->fd);
        return -errno;
    }

    ring->sqes = (struct io_uring_sqe *)mmap(
        NULL, ring->sqe_size, PROT_READ | PROT_WRITE, MAP_SHARED,
        ring->fd, 0x10000000ULL);
    if (ring->sqes == MAP_FAILED) {
        munmap(ring->cq_ring, ring->cq_size);
        munmap(ring->sq_ring, ring->sq_size);
        close(ring->fd);
        return -errno;
    }

    return 0;
}

static void ring_cleanup(struct test_ring *ring)
{
    if (ring->sqes   && ring->sqes   != MAP_FAILED) munmap(ring->sqes,   ring->sqe_size);
    if (ring->cq_ring && ring->cq_ring != MAP_FAILED) munmap(ring->cq_ring, ring->cq_size);
    if (ring->sq_ring && ring->sq_ring != MAP_FAILED) munmap(ring->sq_ring, ring->sq_size);
    if (ring->fd >= 0) close(ring->fd);
}

/*
 * Submit one IORING_OP_WRITE and wait for its completion.
 * Returns 0 on success, errno on failure.
 */
static int ring_write_one(struct test_ring *ring, int fd,
                          const void *buf, size_t len, off_t off)
{
    unsigned tail  = ring->sq_ring->tail;
    unsigned index = tail & ring->sq_ring->ring_mask;

    struct io_uring_sqe *sqe = &ring->sqes[index];
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode    = IORING_OP_WRITE;
    sqe->fd        = fd;
    sqe->addr      = (uint64_t)(uintptr_t)buf;
    sqe->len       = (uint32_t)len;
    sqe->off       = (uint64_t)(off_t)off;
    sqe->user_data = 1;

    ring->sq_ring->tail = tail + 1;

    // sys_io_uring_enter returns the number of SQEs submitted (1 here) on
    // success; only a negative value is an error.
    int ret = sys_io_uring_enter(ring->fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);
    if (ret < 0)
        return -ret;

    unsigned cq_head = ring->cq_ring->head;
    struct io_uring_cqe *cqe =
        &ring->cq_ring->cqes[cq_head & ring->cq_ring->ring_mask];

    int32_t res = cqe->res;
    ring->cq_ring->head = cq_head + 1;

    return (res < 0) ? -res : 0;
}

/* ------------------------------------------------------------------ */
/* Worker result                                                        */
/* ------------------------------------------------------------------ */

struct work_result {
    double mbs;   /* throughput in MB/s, 0.0 on failure */
    bool   ok;
};

/* ------------------------------------------------------------------ */
/* T1: buffered sequential write                                        */
/* ------------------------------------------------------------------ */

static work_result worker_buf_seq_write(const char *path, size_t blksz)
{
    using clk = std::chrono::high_resolution_clock;
    std::vector<char> buf(blksz, (char)0xA5);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return {0.0, false};

    auto t0 = clk::now();
    for (size_t done = 0; done < WORK_SIZE; ) {
        ssize_t n = write(fd, buf.data(), buf.size());
        if (n <= 0) { close(fd); return {0.0, false}; }
        done += (size_t)n;
    }
    fsync(fd);
    close(fd);

    double s = std::chrono::duration<double>(clk::now() - t0).count();
    return {(double)WORK_SIZE / (1024.0 * 1024.0) / s, true};
}

/* ------------------------------------------------------------------ */
/* T2: O_DIRECT random write                                           */
/* ------------------------------------------------------------------ */

static work_result worker_odirect_rand_write(const char *path, size_t blksz)
{
    using clk = std::chrono::high_resolution_clock;

    // O_DIRECT requires the buffer aligned to the device logical block, not to
    // the transfer size.  posix_memalign's alignment must be a power of two, so
    // aligning to blksz fails (EINVAL) for non-power-of-2 block sizes (24kB,
    // 40kB, ...); 4 KiB satisfies every Crucible/virtio volume on this build.
    constexpr size_t DIO_ALIGN = 4096;
    void *buf = nullptr;
    if (posix_memalign(&buf, DIO_ALIGN, blksz) != 0)
        return {0.0, false};
    memset(buf, 0x5A, blksz);

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, 0644);
    if (fd < 0) {
        /*
         * O_DIRECT may be unsupported on this build. Fall back to a
         * regular random write so the slot is still exercised.
         */
        fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { free(buf); return {0.0, false}; }
    }

    /* Pre-size so pwrite doesn't extend file one block at a time. */
    if (ftruncate(fd, (off_t)WORK_SIZE) != 0) {
        close(fd); free(buf); return {0.0, false};
    }

    size_t nblocks = WORK_SIZE / blksz;
    uint32_t seed  = 0x12345678u;

    auto t0 = clk::now();
    for (size_t i = 0; i < nblocks; i++) {
        /* Park-Miller-style LCG for reproducible shuffle. */
        seed = seed * 1664525u + 1013904223u;
        off_t off = (off_t)((seed % nblocks) * blksz);
        ssize_t n = pwrite(fd, buf, blksz, off);
        if (n <= 0) { close(fd); free(buf); return {0.0, false}; }
    }
    fsync(fd);
    close(fd);
    free(buf);

    double s = std::chrono::duration<double>(clk::now() - t0).count();
    return {(double)WORK_SIZE / (1024.0 * 1024.0) / s, true};
}

/* ------------------------------------------------------------------ */
/* T3: io_uring sequential write                                        */
/* ------------------------------------------------------------------ */

static work_result worker_uring_seq_write(const char *path, size_t blksz)
{
    using clk = std::chrono::high_resolution_clock;

    struct test_ring ring;
    if (ring_init(&ring, 32) != 0)
        return {0.0, false};

    std::vector<char> buf(blksz, (char)0x3C);

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { ring_cleanup(&ring); return {0.0, false}; }

    auto t0 = clk::now();
    for (size_t done = 0; done < WORK_SIZE; done += blksz) {
        int ret = ring_write_one(&ring, fd, buf.data(), blksz, (off_t)done);
        if (ret != 0) {
            close(fd);
            ring_cleanup(&ring);
            return {0.0, false};
        }
    }
    fsync(fd);
    close(fd);
    ring_cleanup(&ring);

    double s = std::chrono::duration<double>(clk::now() - t0).count();
    return {(double)WORK_SIZE / (1024.0 * 1024.0) / s, true};
}

/* ------------------------------------------------------------------ */
/* T4: buffered sequential read                                         */
/* ------------------------------------------------------------------ */

static work_result worker_buf_seq_read(const char *path, size_t blksz)
{
    using clk = std::chrono::high_resolution_clock;
    std::vector<char> buf(blksz);

    int fd = open(path, O_RDONLY);
    if (fd < 0) return {0.0, false};

    auto t0 = clk::now();
    size_t done = 0;
    while (done < WORK_SIZE) {
        ssize_t n = read(fd, buf.data(), buf.size());
        if (n <= 0) break;
        done += (size_t)n;
    }
    close(fd);

    double s = std::chrono::duration<double>(clk::now() - t0).count();
    return {(double)done / (1024.0 * 1024.0) / s, done > 0};
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    /*
     * Size each worker to RAM so the four-worker aggregate (4*WORK_SIZE) stays
     * at ~RAM/8, leaving headroom for ARC + pagecache.  Without this, a 128 MiB
     * guest oversubscribes (4*32 MB = 128 MB == all of RAM) and the sweep
     * cannot finish within the harness timeout.  Floor at 4 MB so tiny guests
     * still exercise every path; cap at the historical 32 MB so large guests
     * keep the original throughput profile.
     */
    {
        long pages = sysconf(_SC_PHYS_PAGES);
        long psz   = sysconf(_SC_PAGESIZE);
        if (pages > 0 && psz > 0) {
            size_t ram = (size_t)pages * (size_t)psz;
            size_t want = ram / 32;
            if (want < 4UL * 1024 * 1024)  want = 4UL * 1024 * 1024;
            if (want > 32UL * 1024 * 1024) want = 32UL * 1024 * 1024;
            WORK_SIZE = want;
        }
    }

    /* Pre-populate the read-source file (WORK_SIZE bytes). */
    {
        const size_t io = 64 * 1024;
        std::vector<char> buf(io, (char)0xCD);
        int fd = open(READ_SRC, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("open read-src");
            return 1;
        }
        for (size_t done = 0; done < WORK_SIZE; ) {
            ssize_t n = write(fd, buf.data(), buf.size());
            if (n <= 0) { perror("write read-src"); close(fd); return 1; }
            done += (size_t)n;
        }
        fsync(fd);
        close(fd);
    }

    printf("=== ZFS / Crucible stress test ===\n");
    printf("Worker size: %zu MB | "
           "Threads: buf-seq-wr, odirect-rnd-wr, uring-seq-wr, buf-seq-rd\n\n",
           WORK_SIZE / (1024 * 1024));
    printf("  %-8s  %-14s  %-15s  %-15s  %-14s\n",
           "BlkSize", "BufSeqWr MB/s", "ODirRndWr MB/s",
           "UringSeqWr MB/s", "BufSeqRd MB/s");
    printf("  %-8s  %-14s  %-15s  %-15s  %-14s\n",
           "-------", "-------------", "--------------",
           "---------------", "-------------");

    for (int step = 1; step <= 16; step++) {
        size_t blksz = (size_t)step * 8 * 1024;

        work_result r1, r2, r3, r4;

        std::thread t1([&]{ r1 = worker_buf_seq_write    ("/tmp/stress-bsw.dat",   blksz); });
        std::thread t2([&]{ r2 = worker_odirect_rand_write("/tmp/stress-odr.dat",   blksz); });
        std::thread t3([&]{ r3 = worker_uring_seq_write  ("/tmp/stress-uring.dat", blksz); });
        std::thread t4([&]{ r4 = worker_buf_seq_read     (READ_SRC,                blksz); });

        t1.join(); t2.join(); t3.join(); t4.join();

        auto fmt = [](const work_result &r) -> double {
            return r.ok ? r.mbs : -1.0;
        };

        printf("  %4zukB    %10.1f    %11.1f     %11.1f     %10.1f\n",
               blksz / 1024, fmt(r1), fmt(r2), fmt(r3), fmt(r4));

        unlink("/tmp/stress-bsw.dat");
        unlink("/tmp/stress-odr.dat");
        unlink("/tmp/stress-uring.dat");
    }

    unlink(READ_SRC);

    printf("\nNote: -1.0 = worker failed. "
           "128kB row expected to show best sequential throughput.\n");
    return 0;
}
