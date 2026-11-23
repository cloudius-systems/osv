/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Stress the NVMe driver's handling of a full submission queue: many threads
// do random I/O on a raw block device, so submitters keep finding the queue
// full and waiting for completions to free entries. A race between the
// submitters and the completion thread used to abort the guest with
//     Assertion failed: (((_sq._tail + 1) % _qsize) != _sq._head)
// or could leave a submitter waiting forever.
//
// The queue fills up quickest when the controller has a short queue. Run on
// a 1 GB NVMe data disk whose controller allows only 64 queue entries, e.g.:
//
//   truncate -s 1G /tmp/nvme-data.raw
//   scripts/run.py --nvme --second-nvme-image=/tmp/nvme-data.raw -c 4 \
//       --pass-args="-global nvme.mqes=63" \
//       -e "/tests/misc-nvme-sq-full.so /dev/vblk1"
//
// Usage: misc-nvme-sq-full.so <device> [threads (256)] [seconds (30)] [r|wv]
//   r:  random 4 KB reads from the first 256 MB of the device (the default);
//   wv: each thread writes a unique pattern to random blocks of its own slice
//       of the first 256 MB, reads each back and compares.
// Prints the I/O rate, and fails if any I/O failed or read back wrong data.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <device> [threads] [seconds] [r|wv]\n", argv[0]);
        return 1;
    }
    const char *dev = argv[1];
    int nthreads = argc > 2 ? atoi(argv[2]) : 256;
    double secs = argc > 3 ? atof(argv[3]) : 30;
    std::string mode = argc > 4 ? argv[4] : "r";
    if (nthreads <= 0 || secs <= 0 || (mode != "r" && mode != "wv")) {
        fprintf(stderr, "bad arguments\n");
        return 1;
    }
    const size_t bs = 4096;
    const size_t span = 256u << 20;
    const size_t slice = span / nthreads / bs * bs;
    if (mode == "wv" && slice == 0) {
        fprintf(stderr, "too many threads for wv mode\n");
        return 1;
    }

    std::atomic<bool> stop{false};
    std::atomic<long> ops{0}, mismatches{0}, errors{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < nthreads; i++) {
        threads.emplace_back([&, i] {
            int fd = open(dev, O_RDWR);
            if (fd < 0) {
                errors++;
                return;
            }
            char *w = static_cast<char *>(aligned_alloc(4096, bs));
            char *r = static_cast<char *>(aligned_alloc(4096, bs));
            std::mt19937_64 rng(i * 7919 + 1);
            size_t nblocks = (mode == "r" ? span : slice) / bs;
            long mine = 0, iter = 0;
            while (!stop.load()) {
                size_t block = rng() % nblocks;
                if (mode == "r") {
                    if (pread(fd, r, bs, block * bs) != (ssize_t)bs) {
                        errors++;
                        break;
                    }
                } else {
                    off_t off = i * slice + block * bs;
                    for (size_t k = 0; k < bs; k += 8) {
                        uint64_t v = (uint64_t(i) << 48) ^ (uint64_t(iter) << 20) ^ k;
                        memcpy(w + k, &v, 8);
                    }
                    iter++;
                    if (pwrite(fd, w, bs, off) != (ssize_t)bs ||
                        pread(fd, r, bs, off) != (ssize_t)bs) {
                        errors++;
                        break;
                    }
                    if (memcmp(w, r, bs)) {
                        mismatches++;
                    }
                }
                mine++;
            }
            ops += mine;
            free(w);
            free(r);
            close(fd);
        });
    }
    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::duration<double>(secs));
    stop = true;
    for (auto &t : threads) {
        t.join();
    }
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    printf("misc-nvme-sq-full: mode=%s threads=%d: %.0f I/Os per second, %ld errors, %ld mismatches\n",
           mode.c_str(), nthreads, ops.load() / elapsed, errors.load(), mismatches.load());
    bool ok = !errors.load() && !mismatches.load();
    printf("%s\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
