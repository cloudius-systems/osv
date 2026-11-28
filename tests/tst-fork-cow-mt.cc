/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A copy-on-write fault in a multi-threaded fork child must reach every CPU
// that runs a thread of that child.  The parent fills private pages and forks
// children one after another.  Each child runs one thread per CPU (up to 4),
// pinned, and walks the inherited pages one per round:
//   1. every thread reads the page, so its CPU caches the read-only entry;
//   2. one thread writes a token: the copy-on-write fault;
//   3. every thread reads the token back, then writes its own slot;
//   4. every thread reads every slot back.
// A CPU left with the read-only entry of the old frame reads the old bytes in
// step 3 or 4.  The barriers live in shared memory, which is never COW.

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>

static const int max_threads = 4;
static const size_t npages = 4096;     // rounds per child
static const int nchildren = 25;       // 102400 rounds in all
static const uint64_t fill = 0x5a5a5a5a5a5a5a5aull;

struct shared_state {
    std::atomic<unsigned> arrived;
    std::atomic<unsigned> generation;
    std::atomic<long> stale;
    std::atomic<long> rounds;
};

static shared_state *g_sh;
static char *g_pages;
static int g_nthreads;

static void barrier()
{
    unsigned gen = g_sh->generation.load(std::memory_order_acquire);
    if (g_sh->arrived.fetch_add(1, std::memory_order_acq_rel) + 1 == unsigned(g_nthreads)) {
        g_sh->arrived.store(0, std::memory_order_relaxed);
        g_sh->generation.fetch_add(1, std::memory_order_acq_rel);
        return;
    }
    while (g_sh->generation.load(std::memory_order_acquire) == gen) {
    }
}

static void report(int me, size_t r, const char *what, uint64_t got, uint64_t want)
{
    if (g_sh->stale.fetch_add(1) < 5) {
        printf("FAIL: thread %d round %zu: %s reads %#llx, want %#llx\n", me, r, what,
               (unsigned long long)got, (unsigned long long)want);
    }
}

static void *worker(void *arg)
{
    int me = (int)(long)arg;
    cpu_set_t cs;
    CPU_ZERO(&cs);
    CPU_SET(me, &cs);
    // sched_setaffinity: a fork child's main thread has no pthread object.
    if (sched_setaffinity(0, sizeof(cs), &cs)) {
        report(me, 0, "sched_setaffinity", errno, 0);
    }
    barrier();
    for (size_t r = 0; r < npages; r++) {
        volatile uint64_t *p = reinterpret_cast<volatile uint64_t *>(g_pages + r * 4096);
        uint64_t token = 0x7000000000000000ull | (r << 8);
        int writer = int(r % g_nthreads);
        if (p[0] != fill) {
            report(me, r, "the inherited page", p[0], fill);
        }
        barrier();
        if (me == writer) {
            p[0] = token;
        }
        barrier();
        if (p[0] != token) {
            report(me, r, "the token", p[0], token);
        }
        p[8 * (me + 1)] = token | me;
        barrier();
        for (int t = 0; t < g_nthreads; t++) {
            if (p[8 * (t + 1)] != (token | t)) {
                report(me, r, "a slot", p[8 * (t + 1)], token | t);
            }
        }
        if (me == 0) {
            g_sh->rounds.fetch_add(1, std::memory_order_relaxed);
        }
        barrier();
    }
    return nullptr;
}

static int child()
{
    pthread_t t[max_threads];
    for (int i = 1; i < g_nthreads; i++) {
        if (pthread_create(&t[i], nullptr, worker, (void *)(long)i)) {
            printf("FAIL: pthread_create in the child errno=%d\n", errno);
            _exit(2);
        }
    }
    worker(nullptr);
    for (int i = 1; i < g_nthreads; i++) {
        pthread_join(t[i], nullptr);
    }
    _exit(g_sh->stale.load() ? 1 : 0);
}

int main()
{
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        printf("SUMMARY: 0 failures\n");
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    waitpid(probe, nullptr, 0);
    setvbuf(stdout, nullptr, _IONBF, 0);

    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    g_nthreads = ncpu < max_threads ? int(ncpu) : max_threads;
    if (g_nthreads < 2) {
        printf("SKIP: needs 2 or more CPUs\n");
        printf("SUMMARY: 0 failures\n");
        return 0;
    }
    g_sh = static_cast<shared_state *>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                                            MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    g_pages = static_cast<char *>(mmap(nullptr, npages * 4096, PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (g_sh == MAP_FAILED || g_pages == MAP_FAILED) {
        printf("FAIL: mmap errno=%d\n", errno);
        return 1;
    }
    printf("=== tst-fork-cow-mt: %d children x %zu rounds, %d threads ===\n",
           nchildren, npages, g_nthreads);
    int failures = 0;
    for (int c = 0; c < nchildren; c++) {
        for (size_t i = 0; i < npages * 4096 / 8; i++) {
            reinterpret_cast<uint64_t *>(g_pages)[i] = fill;
        }
        pid_t pid = fork();
        if (pid < 0) {
            printf("FAIL: fork errno=%d\n", errno);
            return 1;
        }
        if (pid == 0) {
            child();
        }
        int st = 0;
        if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st)) {
            failures++;
        }
    }
    long stale = g_sh->stale.load(), rounds = g_sh->rounds.load();
    printf("%ld rounds, %ld stale reads\n", rounds, stale);
    if (rounds != long(nchildren * npages)) {
        printf("FAIL: %ld of %ld rounds ran\n", rounds, long(nchildren * npages));
        failures++;
    }
    if (!failures && !stale) {
        printf("PASS: every CPU saw each copy-on-write fault of its address space\n");
    }
    printf("SUMMARY: %d failures\n", failures + (stale ? 1 : 0));
    return failures || stale ? 1 : 0;
}
