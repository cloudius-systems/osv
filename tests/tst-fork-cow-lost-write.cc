/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test: a parent's writes to private memory must not be lost
 * because of a fork().
 *
 * fork() write-protects the parent's private pages so the first write after
 * it copies the page (copy-on-write).  Every CPU that still holds a writable
 * TLB entry for such a page must drop it before any thread can take the
 * copy-on-write fault, or that CPU keeps writing the old frame -- which only
 * the child maps once the parent has its copy -- and the parent loses those
 * writes.  clone_address_space() released the address-space lock before
 * flushing, so a copy-on-write fault on another CPU could run in that window.
 * In tst-fork-concfork-objlist this lost a relink of the kernel's own vma
 * tree (whose nodes can sit in that memory) and a later fork walked into an
 * erased node with the lock held (the silent hang).
 *
 * Writer threads each own one word of a single .bss page.  Each stores 1, 2,
 * 3, ... and checks before every store that the word still holds its previous
 * store.  Forker threads fork and reap children that exit at once.  A word
 * that reads back an older value is a lost write.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <ctime>

static constexpr int nwriters = 8;

struct alignas(4096) page {
    volatile unsigned long word[nwriters * 8];   // one cache line per writer
};
static page g_page;

static std::atomic<bool> g_stop{false};
static std::atomic<long> g_lost{0};
static std::atomic<long> g_forks{0};
static std::atomic<long> g_stores{0};

static void *writer(void *arg)
{
    long i = (long)arg;
    volatile unsigned long *w = &g_page.word[i * 8];
    unsigned long n = 0;
    *w = 0;
    while (!g_stop.load(std::memory_order_relaxed)) {
        unsigned long v = *w;
        if (v != n) {
            if (g_lost.fetch_add(1) < 5) {
                printf("FAIL: writer %ld wrote %lu, reads back %lu\n", i, n, v);
            }
        }
        *w = ++n;
    }
    g_stores.fetch_add(n);
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
    setvbuf(stdout, nullptr, _IONBF, 0);
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    const int nforkers = 4;
    printf("=== tst-fork-cow-lost-write (%d s, %d writers, %d forkers) ===\n",
           seconds, nwriters, nforkers);
    pthread_t w[nwriters], f[nforkers];
    for (long i = 0; i < nwriters; i++) {
        pthread_create(&w[i], nullptr, writer, (void *)i);
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
    for (auto &t : w) {
        pthread_join(t, nullptr);
    }
    printf("%ld forks, %ld stores, %ld lost writes\n", g_forks.load(),
           g_stores.load(), g_lost.load());
    if (g_forks.load() == 0) {
        printf("FAIL: no fork completed\n");
        return 1;
    }
    if (g_lost.load()) {
        printf("SUMMARY: FAIL\n");
        return 1;
    }
    printf("SUMMARY: 0 failures\n");
    return 0;
}
