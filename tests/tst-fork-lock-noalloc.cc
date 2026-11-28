/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test: a contended lockfree::mutex::lock() must not allocate
 * memory while a fork child is alive.
 *
 * While any fork child exists, lock() must keep its wait_record off the
 * waiter's stack (another address space may dereference it).  It used to
 * malloc that record AFTER bumping the mutex's waiter count.  When the malloc
 * needed a fresh page it took free_page_ranges_lock; if that was the contended
 * mutex, the nested lock() bumped the count again and allocated again, and the
 * lock could never be handed over because no waiter was ever queued.  The
 * recursion ran until the thread overflowed its stack (in
 * tst-fork-concfork-objlist: a fault in the stack guard page, RIP in
 * mutex::lock or the page pool).
 *
 * The recursion itself needs the page pools to run dry at the right moment, so
 * this test checks the precondition directly: with a fork child alive, it
 * makes one thread wait on a held mutex many times and counts allocations made
 * by that thread inside lock().  Any allocation fails the test.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <cstdio>
#include <atomic>
#include <string>
#include <vector>
#include <osv/trace.hh>
#include <osv/mutex.h>
#include <osv/sched.hh>
#include <osv/elf.hh>
#include <cerrno>

// The probe's hit() runs inside the allocator; keep this object resident.
OSV_ELF_MLOCK_OBJECT();

struct thread_probe : tracepoint_base::probe {
    std::atomic<sched::thread *> target{nullptr};
    std::atomic<long> hits{0};
    void hit() override {
        if (sched::thread::current() == target.load(std::memory_order_relaxed)) {
            hits.fetch_add(1, std::memory_order_relaxed);
        }
    }
};

static mutex g_mtx;
static std::atomic<int> g_held{0};
static std::atomic<int> g_go{0};
static const int rounds = 200;

static void *holder(void *)
{
    for (int i = 0; i < rounds; i++) {
        while (g_go.load() != i + 1) {
            sched::thread::yield();
        }
        g_mtx.lock();
        g_held.store(i + 1);
        usleep(2000);   // long enough for the waiter to reach the slow path
        g_mtx.unlock();
    }
    return nullptr;
}

int main()
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
    printf("=== tst-fork-lock-noalloc ===\n");

    thread_probe allocs, waits;
    std::vector<std::pair<tracepoint_base *, thread_probe *>> attached;
    for (auto &tp : tracepoint_base::tp_list) {
        std::string n(tp.name);
        if (n == "memory_malloc" || n == "memory_malloc_mempool" ||
            n == "memory_malloc_page" || n == "memory_malloc_large") {
            attached.push_back({&tp, &allocs});
        } else if (n == "mutex_lock_wait") {
            attached.push_back({&tp, &waits});
        }
    }
    if (attached.size() < 3) {
        printf("FAIL: tracepoints not found (%zu)\n", attached.size());
        return 1;
    }
    for (auto &a : attached) {
        a.first->add_probe(a.second);
    }

    int pfd[2];
    if (pipe(pfd)) {
        perror("pipe");
        return 1;
    }
    pid_t child = fork();
    if (child < 0) {
        printf("SKIP: fork() unavailable\n");
        return 0;
    }
    if (child == 0) {
        char c;
        read(pfd[0], &c, 1);   // stay alive until the parent is done
        _exit(0);
    }

    pthread_t h;
    pthread_create(&h, nullptr, holder, nullptr);
    auto self = sched::thread::current();
    for (int i = 0; i < rounds; i++) {
        g_go.store(i + 1);
        while (g_held.load() != i + 1) {
            sched::thread::yield();
        }
        allocs.target.store(self);
        waits.target.store(self);
        g_mtx.lock();
        allocs.target.store(nullptr);
        waits.target.store(nullptr);
        g_mtx.unlock();
    }
    pthread_join(h, nullptr);

    write(pfd[1], "x", 1);
    int st;
    waitpid(child, &st, 0);

    for (auto &a : attached) {
        a.first->del_probe(a.second);
    }

    long a = allocs.hits.load(), w = waits.hits.load();
    printf("%d contended lock() calls with a live fork child: %ld waited, "
           "%ld allocations\n", rounds, w, a);
    if (w < rounds / 2) {
        printf("FAIL: only %ld of %d lock() calls waited; test did not "
               "exercise the contended path\n", w, rounds);
        return 1;
    }
    if (a) {
        printf("SUMMARY: FAIL\n");
        return 1;
    }
    printf("SUMMARY: 0 failures\n");
    return 0;
}
