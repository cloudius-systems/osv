/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test: a shared_ptr's reference count must not drift because of
 * a fork().
 *
 * The count lives in the parent's private memory, which fork()
 * write-protects for copy-on-write.  If a CPU keeps a writable TLB entry for
 * the old frame after another thread has copied the page, an increment can
 * land in the old frame (which only the child maps now) and the matching
 * decrement in the parent's copy.  The parent's count then drifts down, and
 * the object is destroyed while it is still owned.  In PostgreSQL this
 * dropped the main executable during a fork and faulted in
 * elf::object::soname().
 *
 * Copier threads copy and drop one shared_ptr in a loop while forker threads
 * fork.  The test fails if the deleter runs while the pointer is still owned,
 * or if the count is not 1 at the end.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <memory>
#include <ctime>
#include <cerrno>

static constexpr int ncopiers = 8;
static constexpr int nforkers = 4;

static std::shared_ptr<long> g_ptr;
static std::atomic<bool> g_stop{false};
static std::atomic<long> g_deleted{0};
static std::atomic<long> g_forks{0};
static std::atomic<long> g_copies{0};

static void *copier(void *)
{
    long n = 0;
    while (!g_stop.load(std::memory_order_relaxed)) {
        auto c = g_ptr;
        ++*c;
        n++;
    }
    g_copies.fetch_add(n);
    return nullptr;
}

static void *forker(void *)
{
    while (!g_stop.load(std::memory_order_relaxed)) {
        pid_t p = fork();
        if (p == 0) {
            _exit(0);
        }
        if (p > 0) {
            int st;
            waitpid(p, &st, 0);
            g_forks.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return nullptr;
}

int main(int argc, char **argv)
{
    // fork() is opt-in (CONFIG_fork). When it is off it returns -1/ENOSYS;
    // skip cleanly, as tst-fork does, so the default test suite stays green.
    {
        errno = 0;
        pid_t probe = fork();
        if (probe == -1 && errno == ENOSYS) {
            printf("SKIP: fork() not enabled (CONFIG_fork off)\n");
            return 0;
        }
        if (probe == 0) {
            _exit(0);   // the probe fork's child: exit immediately
        }
        if (probe > 0) {
            int st = 0;
            waitpid(probe, &st, 0);   // reap the probe child
        }
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    printf("=== tst-fork-sharedptr-drift (%d s, %d copiers, %d forkers) ===\n",
           seconds, ncopiers, nforkers);
    // The deleter only counts: the object must stay readable if the count
    // drifts to zero early, so the test reports instead of crashing.
    g_ptr = std::shared_ptr<long>(new long(0), [](long *) { g_deleted++; });
    pthread_t c[ncopiers], f[nforkers];
    for (auto &t : c) {
        pthread_create(&t, nullptr, copier, nullptr);
    }
    for (auto &t : f) {
        pthread_create(&t, nullptr, forker, nullptr);
    }
    struct timespec ts = { seconds, 0 };
    nanosleep(&ts, nullptr);
    g_stop.store(true);
    for (auto &t : f) {
        pthread_join(t, nullptr);
    }
    for (auto &t : c) {
        pthread_join(t, nullptr);
    }
    long uc = g_ptr.use_count();
    long del = g_deleted.load();
    printf("%ld forks, %ld copies, use_count %ld (want 1), "
           "deleter ran %ld times (want 0)\n",
           g_forks.load(), g_copies.load(), uc, del);
    if (g_forks.load() == 0) {
        printf("FAIL: no fork completed\n");
        return 1;
    }
    if (uc != 1 || del != 0) {
        printf("SUMMARY: FAIL\n");
        return 1;
    }
    printf("SUMMARY: 0 failures\n");
    return 0;
}
