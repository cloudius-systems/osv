/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test: a written page of a private FILE mapping (here this
 * object's own .data) must keep its written contents when several threads
 * write-fault it while other threads fork().
 *
 * Every fork() makes the parent's private writable pages copy-on-write again.
 * When two threads write-fault the same COW page, the first copies it and the
 * second finds a plain writable page, drops the address-space lock and falls
 * through to the ordinary fault path.  If a fork() re-protects the page in that
 * gap, the ordinary path sees a COW page again.  For an anonymous page that is
 * harmless, but for a file-backed page the page cache treats it as a read-only
 * cache mapping and replaces it with a fresh copy of the FILE, discarding what
 * the process wrote: relocated .got/.got.plt slots and written .data/.bss go
 * back to their on-disk bytes.  In tst-fork-concfork-objlist this showed up as
 * jumps through unrelocated PLT slots (rip=0x1056) and calls through a function
 * pointer read back as file bytes ("22 (Red " from .comment).
 *
 * The test keeps a canary in a page-aligned .data page, writes it to a value
 * that differs from the file, then has writer threads hammer that page while
 * forker threads fork continuously.  Any writer that reads the file's value
 * back reports FAIL.  The children exit at once, so the test only exercises the
 * parent's COW handling.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <ctime>
#include <cerrno>

static constexpr long file_value = 0x46494c45;     // "FILE": what the image holds
static constexpr long written_value = 0x57524954;  // "WRIT": what we store

// Initialized (non-zero) so it is in .data, i.e. backed by the file; aligned
// so the canary and the counters share one page and nothing else does.
struct alignas(4096) shared_page {
    volatile long canary;
    std::atomic<long> counters[8];
};
static shared_page g_page = { file_value, {} };

static std::atomic<bool> g_stop{false};
static std::atomic<long> g_reverted{0};
static std::atomic<long> g_forks{0};

static void *writer(void *arg)
{
    long i = (long)arg;
    while (!g_stop.load(std::memory_order_relaxed)) {
        g_page.counters[i & 7].fetch_add(1, std::memory_order_relaxed);
        long v = g_page.canary;
        if (v != written_value) {
            if (g_reverted.fetch_add(1) == 0) {
                printf("FAIL: canary is 0x%lx, expected 0x%lx (file value 0x%lx)\n",
                       v, written_value, file_value);
            }
            g_page.canary = written_value;   // keep going, count every hit
        }
    }
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
    const int nwriters = 6, nforkers = 4;
    printf("=== tst-fork-cow-file-race (%d s, %d writers, %d forkers) ===\n",
           seconds, nwriters, nforkers);

    g_page.canary = written_value;

    pthread_t w[nwriters], f[nforkers];
    for (long i = 0; i < nwriters; i++) {
        pthread_create(&w[i], nullptr, writer, (void *)i);
    }
    for (int i = 0; i < nforkers; i++) {
        pthread_create(&f[i], nullptr, forker, nullptr);
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

    long reverted = g_reverted.load();
    printf("%ld forks, canary reverted to file contents %ld times\n",
           g_forks.load(), reverted);
    if (g_forks.load() == 0) {
        printf("FAIL: no fork completed\n");
        return 1;
    }
    if (reverted) {
        printf("SUMMARY: FAIL\n");
        return 1;
    }
    printf("SUMMARY: 0 failures\n");
    return 0;
}
