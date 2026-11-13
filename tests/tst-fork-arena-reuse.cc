/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Fork-arena VA of an exited process is reused, but never by a process that
// maps part of it.  main forks P, which fills part of its first arena extent
// and exits.  A new child C then maps a page of its own (MAP_FIXED) inside P's
// old extent, stamps it, and allocates across three extents: C must not be
// given P's extent, and its page must survive.  New children of main, which
// do not map it, then allocate until one of them is given P's extent.
//
// If fork() works in a fork child, P also forks G before exiting: G inherits
// P's chunks, allocates past an extent after P is reaped, and must neither be
// given P's extent nor see the inherited chunks change.
//
// Extents are 64 MiB, numbered from the start of the arena range, and are
// computed from pointer values only.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-arena-reuse requires CONF_fork\n");
    return 0;
}

#else

#include <osv/fork_arena.hh>
#include <atomic>
#include <new>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static const uintptr_t arena_start = 96ull << 39;
static const int nlive = 2000;
static const size_t live_sz = 200;
static const size_t big_sz = 256 * 1024 - 16;   // one 256 KiB class
static const int c_chunks = 768;                // 192 MiB: three extents
static const int g_chunks = 320;                // 80 MiB: past one extent
static const int max_siblings = 32;
static const uint64_t page_tag = 0x0dd5a1e5ca1ab1e5ULL;

static long extent_of(const void *p)
{
    return long((reinterpret_cast<uintptr_t>(p) - arena_start) >> 26);
}

struct shared {
    std::atomic<int> phase;        // 1: P reaped, 2: release everyone
    std::atomic<long> p_extent;
    std::atomic<uintptr_t> p_first;
    std::atomic<int> p_spread;     // P's chunks were not all in one extent
    std::atomic<int> g_pid, g_errno, g_done;
    std::atomic<int> g_overlap, g_corrupt, g_extents;
    std::atomic<int> c_overlap, c_extents, c_page_lost, c_map_failed;
    std::atomic<int> outside;
    std::atomic<long> sib_extent[max_siblings];
    std::atomic<int> sib_ready[max_siblings];
};
static shared *sh;
static void *live[nlive];   // COW-inherited by G

static uint64_t live_word(void *p, size_t i)
{
    return (0x5eed5eed00000000ULL ^ reinterpret_cast<uintptr_t>(p)) + i;
}

static int corrupt_count()
{
    int bad = 0;
    for (auto p : live) {
        auto q = static_cast<uint64_t*>(p);
        for (size_t i = 0; i < live_sz / 8; i++) {
            if (q[i] != live_word(p, i)) {
                bad++;
                break;
            }
        }
    }
    return bad;
}

static void wait_for(std::atomic<int> &v, int at_least)
{
    while (v.load() < at_least) {
        usleep(2000);
    }
}

// Allocate n big chunks, never freeing; count those in P's extent and the
// extents used, and call check() every 32 chunks.
template<typename F>
static void grow(int n, std::atomic<int> &overlap, std::atomic<int> &extents, F check)
{
    long pe = sh->p_extent.load(), last = -1;
    int used = 0;
    for (int i = 0; i < n; i++) {
        char *p = static_cast<char*>(malloc(big_sz));
        if (!p || !fork_arena::contains(p)) {
            sh->outside++;
            break;
        }
        memset(p, 0x77, big_sz);
        if (extent_of(p) == pe) {
            overlap++;
        }
        if (extent_of(p) != last) {
            last = extent_of(p);
            used++;
        }
        if (i % 32 == 31) {
            check();
        }
    }
    check();
    extents = used;
}

static void run_g()
{
    wait_for(sh->phase, 1);
    grow(g_chunks, sh->g_overlap, sh->g_extents, [] {
        int c = corrupt_count();
        if (c > sh->g_corrupt) {
            sh->g_corrupt = c;
        }
    });
    sh->g_done = 1;
    wait_for(sh->phase, 2);
    _exit(0);
}

static void run_p()
{
    for (auto &p : live) {
        p = malloc(live_sz);
        auto q = static_cast<uint64_t*>(p);
        for (size_t i = 0; i < live_sz / 8; i++) {
            q[i] = live_word(p, i);
        }
    }
    long e = extent_of(live[0]);
    for (auto p : live) {
        if (!fork_arena::contains(p) || extent_of(p) != e) {
            sh->p_spread = 1;
        }
    }
    sh->p_extent = e;
    sh->p_first = reinterpret_cast<uintptr_t>(live[0]);
    pid_t g = fork();
    if (g == 0) {
        run_g();
    }
    sh->g_errno = g < 0 ? errno : 0;
    sh->g_pid = g;
    _exit(0);
}

static void run_c()
{
    // A page of C's own in the part of P's extent P had mapped.
    auto page = reinterpret_cast<uint64_t*>(sh->p_first.load() & ~uintptr_t(4095));
    void *m = mmap(page, 4096, PROT_READ | PROT_WRITE,
                   MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m != page) {
        sh->c_map_failed = 1;
        _exit(0);
    }
    for (int i = 0; i < 512; i++) {
        page[i] = page_tag + i;
    }
    grow(c_chunks, sh->c_overlap, sh->c_extents, [page] {
        for (int i = 0; i < 512; i++) {
            if (page[i] != page_tag + uint64_t(i)) {
                sh->c_page_lost = 1;
                return;
            }
        }
    });
    _exit(0);
}

int main()
{
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    waitpid(probe, nullptr, 0);

    void *m = mmap(nullptr, sizeof(shared), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        printf("FAIL: mmap\n");
        return 1;
    }
    sh = new (m) shared{};
    int failures = 0;

    pid_t p = fork();
    if (p == 0) {
        run_p();
    }
    waitpid(p, nullptr, 0);
    // waitpid() returns before the reaper has torn P's address space down.
    sleep(2);
    long pe = sh->p_extent.load();
    pid_t g = sh->g_pid.load();
    printf("P used extent %ld\n", pe);
    if (sh->p_spread) {
        printf("FAIL: setup (P's chunks spread over extents)\n");
        failures++;
    }

    sh->phase = 1;
    if (g > 0) {
        wait_for(sh->g_done, 1);
        printf("G: %d extents, %d chunks in P's extent, %d inherited chunks clobbered\n",
               sh->g_extents.load(), sh->g_overlap.load(), sh->g_corrupt.load());
        if (sh->g_extents < 2 || sh->g_overlap || sh->g_corrupt) {
            printf("FAIL: a process that inherited P's extent was given it, or its data changed\n");
            failures++;
        }
    } else {
        printf("NOTE: fork() in a fork child failed (errno %d): inherited-extent case not run\n",
               sh->g_errno.load());
    }

    pid_t c = fork();
    if (c == 0) {
        run_c();
    }
    waitpid(c, nullptr, 0);
    sleep(2);
    printf("C: %d extents, %d chunks in P's extent, its own page there %s\n",
           sh->c_extents.load(), sh->c_overlap.load(),
           sh->c_map_failed ? "not mapped" : sh->c_page_lost ? "overwritten" : "intact");
    if (sh->c_map_failed || sh->c_extents < 3) {
        printf("FAIL: setup (C could not map its page, or did not grow across three extents)\n");
        failures++;
    }
    if (sh->c_overlap || sh->c_page_lost) {
        printf("FAIL: a process was given an extent it maps part of\n");
        failures++;
    }
    if (sh->outside) {
        printf("FAIL: %d allocations outside the fork arena\n", sh->outside.load());
        failures++;
    }

    pid_t sib[max_siblings];
    int nsib = 0, got = -1;
    for (; nsib < max_siblings && got < 0; nsib++) {
        pid_t s = fork();
        if (s == 0) {
            void *q = malloc(live_sz);
            sh->sib_extent[nsib] = fork_arena::contains(q) ? extent_of(q) : -1;
            sh->sib_ready[nsib] = 1;
            wait_for(sh->phase, 2);
            _exit(0);
        }
        sib[nsib] = s;
        wait_for(sh->sib_ready[nsib], 1);
        if (sh->sib_extent[nsib] == pe) {
            got = nsib;
        }
    }
    printf("new children: %d forked, P's extent %s\n", nsib,
           got >= 0 ? "given to one of them" : "never given out");
    if (got < 0) {
        printf("FAIL: an exited process's extent was not reused\n");
        failures++;
    }
    sh->phase = 2;
    for (int i = 0; i < nsib; i++) {
        waitpid(sib[i], nullptr, 0);
    }
    if (!failures) {
        printf("PASS: tst-fork-arena-reuse\n");
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}

#endif
