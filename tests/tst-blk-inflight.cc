/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Keep a fixed number of reads in flight on a block device for many
// completions, and fail if a completion never arrives.
//
// Each reader thread keeps <depth> 4 KiB reads of random sectors queued
// through the device's strategy routine and issues a new one from every
// completion, until it has issued its share of <reads>.  One reader is
// pinned to each CPU, so a multiqueue driver sees every IO queue.  A lost
// completion interrupt leaves a read pending forever: the watchdog then
// fails the test.  The reads touch only the first 64 MiB of the device and
// write nothing, but give it a scratch disk anyway.
//
// Usage: tst-blk-inflight.so <dev> [reads] [depth], e.g.
//   tst-blk-inflight.so vblk1 100000 32
// With no device the test prints SKIP, so the standard run does not use it.

#include <osv/bio.h>
#include <osv/device.h>
#include <osv/prex.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <random>
#include <thread>
#include <unistd.h>
#include <vector>

static const size_t io_size = 4096;
static const uint64_t span = 64 << 20;

// Seconds the whole run may take; ~100x what 10^5 reads need on QEMU.
static const int watchdog_seconds = 300;

struct reader {
    struct device *dev;
    long budget;
    long issued = 0;
    long inflight = 0;
    long completed = 0;
    long errors = 0;
    std::mt19937_64 rng;
    std::mutex mtx;
    std::condition_variable cv;
};

// Completion callback, run by the driver's completion thread: count the
// read and wake the reader.  It never submits, so it cannot block on a full
// submission queue it is itself responsible for draining.
static void read_done(struct bio *bio)
{
    reader *r = static_cast<reader *>(bio->bio_caller1);
    bool err = bio->bio_flags & BIO_ERROR;
    free(bio->bio_data);
    destroy_bio(bio);
    std::lock_guard<std::mutex> lk(r->mtx);
    r->inflight--;
    r->completed++;
    r->errors += err;
    r->cv.notify_one();
}

// Keep <depth> reads in flight from the calling thread (pinned to one CPU,
// so its submissions go to that CPU's queue) until the budget is issued,
// then wait for the last completions.
static void run_reader(reader *r, int depth)
{
    std::unique_lock<std::mutex> lk(r->mtx);
    while (r->issued < r->budget) {
        r->cv.wait(lk, [r, depth] { return r->inflight < depth; });
        uint64_t off = (r->rng() % (span / io_size)) * io_size;
        r->issued++;
        r->inflight++;
        lk.unlock();
        struct bio *bio = alloc_bio();
        bio->bio_cmd = BIO_READ;
        bio->bio_dev = r->dev;
        bio->bio_data = aligned_alloc(io_size, io_size);
        bio->bio_offset = off;
        bio->bio_bcount = io_size;
        bio->bio_caller1 = r;
        bio->bio_done = read_done;
        r->dev->driver->devops->strategy(bio);
        lk.lock();
    }
    r->cv.wait(lk, [r] { return r->inflight == 0; });
}

// Fail the test if the run has not finished within watchdog_seconds.  main()
// sets <done> and joins the watchdog at the end, because OSv waits for every
// thread of the application before it exits.  The watchdog polls once a
// second rather than using a timed condition wait, which libstdc++
// implements with pthread_cond_clockwait(), a stub on OSv.
static std::atomic<bool> done;

static void watchdog()
{
    for (int s = 0; s < watchdog_seconds; s++) {
        if (done.load()) {
            return;
        }
        sleep(1);
    }
    static const char msg[] = "FAIL: reads did not complete (lost interrupt?)\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(1);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("SKIP: no device given\n");
        return 0;
    }
    long reads = argc > 2 ? atol(argv[2]) : 100000;
    int depth = argc > 3 ? atoi(argv[3]) : 32;
    struct device *dev;
    if (device_open(argv[1], DO_RDWR, &dev)) {
        printf("FAIL: cannot open %s\n", argv[1]);
        return 1;
    }
    std::thread wd(watchdog);

    int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    std::vector<reader> readers(ncpus);
    std::vector<std::thread> threads;
    auto t0 = std::chrono::steady_clock::now();
    for (int cpu = 0; cpu < ncpus; cpu++) {
        readers[cpu].dev = dev;
        readers[cpu].budget = reads / ncpus + (cpu < reads % ncpus);
        readers[cpu].rng.seed(cpu + 1);
        threads.emplace_back([&readers, cpu, depth] {
            cpu_set_t set;
            CPU_ZERO(&set);
            CPU_SET(cpu, &set);
            pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
            run_reader(&readers[cpu], depth);
        });
    }
    long completed = 0, errors = 0;
    for (int cpu = 0; cpu < ncpus; cpu++) {
        threads[cpu].join();
        completed += readers[cpu].completed;
        errors += readers[cpu].errors;
    }
    double secs = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();
    done.store(true);
    wd.join();
    device_close(dev);
    printf("%ld reads completed on %d CPUs at depth %d in %.1f s, %ld errors\n",
           completed, ncpus, depth, secs, errors);
    if (completed != reads || errors) {
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
