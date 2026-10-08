/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Concurrency stress for the dentry cache, aimed at the lock-order inversion
// between dentry_hash_lock and the vnode lock.
//
// The wedge this reproduces: namei() holds vn_lock(dvp) while calling
// dentry_lookup()/dentry_alloc(), which take dentry_hash_lock -- so the order
// is vn_lock -> dentry_hash_lock.  drele() used to take dentry_hash_lock and
// then call vn_del_name(), which takes vn_lock internally -- the reverse.
// Two threads hitting both orders on the same directory vnode deadlock with
// nothing runnable.
//
// To hit it you need many threads doing open()/close() of DIFFERENT names in
// the SAME directory (so they share the parent directory's vnode but land on
// different hash buckets), plus concurrent rename() and unlink() traffic to
// exercise dentry_move()/dentry_remove() and force dentries onto the fake
// fake list.  A single-threaded test never sees it.
//
// On unfixed code this hangs (the harness kills it by timeout).  On fixed
// code it completes and reports OK.  Under DEBUG_VFS the vn_lock() assertion
// fires on the unfixed path instead of hanging, which is the faster signal.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static std::atomic<int> failures{0};
static std::atomic<long> ops{0};

#define EXPECT(cond, msg)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__,  \
                        (msg), errno, std::strerror(errno));                   \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static const char *DIR = "/tmp/tst-dentry-lock";

// Open and close the same set of names repeatedly.  All of these live in one
// directory, so every lookup takes that directory's vnode lock and then a
// dentry_hash_lock; the names differ so they spread across hash buckets.
static void open_close_worker(int id, int iters, int names)
{
    for (int i = 0; i < iters; i++) {
        for (int n = 0; n < names; n++) {
            char path[256];
            std::snprintf(path, sizeof(path), "%s/f%d", DIR, (id + n * 7) % names);
            int fd = open(path, O_RDONLY);
            if (fd >= 0) {
                close(fd);
                ops++;
            }
            // A miss is fine and is itself interesting: a failed lookup still
            // walks the chain and still drele()s the parent dentry.
        }
    }
}

// Stat a deep path so each call walks several components, taking and dropping
// a directory dentry reference at every level -- this is the drele() side.
static void stat_worker(int iters)
{
    for (int i = 0; i < iters; i++) {
        struct stat st;
        char path[256];
        std::snprintf(path, sizeof(path), "%s/d0/d1/d2", DIR);
        if (stat(path, &st) == 0) {
            ops++;
        }
        std::snprintf(path, sizeof(path), "%s/d0/d1", DIR);
        if (stat(path, &st) == 0) {
            ops++;
        }
    }
}

// Rename traffic: exercises dentry_move() and the
// strdup that used to happen under the lock.
static void rename_worker(int id, int iters)
{
    for (int i = 0; i < iters; i++) {
        char a[256], b[256];
        std::snprintf(a, sizeof(a), "%s/r%d_%d", DIR, id, i & 1);
        std::snprintf(b, sizeof(b), "%s/r%d_%d", DIR, id, (i & 1) ^ 1);
        int fd = open(a, O_CREAT | O_RDWR, 0644);
        if (fd >= 0) {
            close(fd);
            if (rename(a, b) == 0) {
                ops++;
            }
            unlink(a);
            unlink(b);
        }
    }
}

// Create/unlink churn: forces dentry_remove(), which moves dentries onto the
// fake list.
static void churn_worker(int id, int iters)
{
    for (int i = 0; i < iters; i++) {
        char path[256];
        std::snprintf(path, sizeof(path), "%s/c%d_%d", DIR, id, i % 4);
        int fd = open(path, O_CREAT | O_RDWR, 0644);
        if (fd >= 0) {
            close(fd);
            // Open it again by name so a second dentry reference exists while
            // it is being removed.
            int fd2 = open(path, O_RDONLY);
            unlink(path);
            if (fd2 >= 0) {
                close(fd2);
            }
            ops++;
        }
    }
}

int main(int argc, char **argv)
{
    // Deliberately more threads than CPUs: the inversion needs two threads to
    // interleave inside the two lock acquisitions, so oversubscription helps.
    int nthreads = 16;
    int iters = 200;
    const int names = 32;   // == DENTRY_BUCKETS, to spread across all buckets
    if (argc > 1) nthreads = std::atoi(argv[1]);
    if (argc > 2) iters = std::atoi(argv[2]);

    std::printf("tst-dentry-lock: %d threads x %d iters\n", nthreads, iters);

    mkdir(DIR, 0777);
    char p[256];
    std::snprintf(p, sizeof(p), "%s/d0", DIR);            mkdir(p, 0777);
    std::snprintf(p, sizeof(p), "%s/d0/d1", DIR);         mkdir(p, 0777);
    std::snprintf(p, sizeof(p), "%s/d0/d1/d2", DIR);      mkdir(p, 0777);
    for (int n = 0; n < names; n++) {
        std::snprintf(p, sizeof(p), "%s/f%d", DIR, n);
        int fd = open(p, O_CREAT | O_RDWR, 0644);
        EXPECT(fd >= 0, "setup open");
        if (fd >= 0) close(fd);
    }

    std::vector<std::thread> ts;
    for (int i = 0; i < nthreads; i++) {
        switch (i % 4) {
        case 0: ts.emplace_back(open_close_worker, i, iters, names); break;
        case 1: ts.emplace_back(stat_worker, iters); break;
        case 2: ts.emplace_back(rename_worker, i, iters); break;
        default: ts.emplace_back(churn_worker, i, iters); break;
        }
    }
    for (auto &t : ts) {
        t.join();
    }

    // Cleanup (best effort; failures here are not the point of the test).
    for (int n = 0; n < names; n++) {
        std::snprintf(p, sizeof(p), "%s/f%d", DIR, n);
        unlink(p);
    }
    std::snprintf(p, sizeof(p), "%s/d0/d1/d2", DIR);  rmdir(p);
    std::snprintf(p, sizeof(p), "%s/d0/d1", DIR);     rmdir(p);
    std::snprintf(p, sizeof(p), "%s/d0", DIR);        rmdir(p);
    rmdir(DIR);

    std::printf("tst-dentry-lock: %ld ops, %d failures\n", ops.load(),
                failures.load());
    if (failures) {
        std::printf("FAILED\n");
        return 1;
    }
    // Reaching this line at all is the result: the unfixed kernel never does.
    std::printf("OK\n");
    return 0;
}
