/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Small multiqueue completion smoke test: drive concurrent writes+reads from
// several threads (so bios are steered across all virtio-blk queues by CPU),
// with a bounded total size, and assert they all COMPLETE.  This distinguishes
// a real multiqueue-completion hang (a bio submitted to queue N whose interrupt
// is never serviced) from mere throughput slowness: the data here is tiny, so
// if any thread's write/read never returns, the test hangs (and the harness
// times out) instead of just being slow.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <vector>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>

static const char *DIR = "/tmp";   // writable on the zfs image root
static const int NTHREADS = 8;
static const size_t PER_THREAD = 4 * 1024 * 1024;   // 4 MiB each -> 32 MiB total
static const size_t CHUNK = 64 * 1024;

static std::atomic<int> failures{0};

static void worker(int id)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/mq-smoke-%d", DIR, id);
    int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) { failures++; return; }

    std::vector<char> buf(CHUNK, (char)(0x40 + id));
    size_t written = 0;
    while (written < PER_THREAD) {
        ssize_t n = write(fd, buf.data(), CHUNK);
        if (n != (ssize_t)CHUNK) { failures++; goto done; }
        written += n;
    }
    if (fsync(fd) != 0) { failures++; goto done; }

    // Read it back and verify - read completions also route per-queue.
    lseek(fd, 0, SEEK_SET);
    {
        std::vector<char> rbuf(CHUNK);
        size_t read_total = 0;
        while (read_total < PER_THREAD) {
            ssize_t n = read(fd, rbuf.data(), CHUNK);
            if (n != (ssize_t)CHUNK) { failures++; break; }
            if (memcmp(rbuf.data(), buf.data(), CHUNK) != 0) { failures++; break; }
            read_total += n;
        }
    }
done:
    close(fd);
    unlink(path);
}

int main()
{
    fprintf(stderr, "mq-smoke: %d threads x %zu bytes\n", NTHREADS, PER_THREAD);
    std::vector<std::thread> ts;
    for (int i = 0; i < NTHREADS; i++)
        ts.emplace_back(worker, i);
    for (auto &t : ts)
        t.join();

    if (failures.load() != 0) {
        fprintf(stderr, "mq-smoke FAILED (%d failures)\n", failures.load());
        return 1;
    }
    fprintf(stderr, "mq-smoke PASSED\n");
    return 0;
}
