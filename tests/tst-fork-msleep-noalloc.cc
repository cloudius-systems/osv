/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Regression test: a BSD msleep() must not allocate memory while a fork
 * child is alive.
 *
 * Under CONF_fork, msleep()'s waiter cannot live on the sleeper's stack: a
 * wakeup from another address space follows the sleep queue into it, and a
 * forked process's stack is private to its own address space.  It used to be
 * allocated on the kernel heap for every sleep.  Every ZFS zio taskqueue
 * worker re-sleeps through msleep() after almost every task, so that was an
 * allocation per dispatch.  It now comes from a per-thread TLS slot, which is
 * on the identity heap.
 *
 * With a fork child alive, one thread sleeps on a channel and another wakes
 * it, many times, and then the same thread sleeps with a timeout nobody
 * wakes, to cover the timed-out path.  Allocations the sleeping thread makes
 * inside msleep() are counted per phase, and any allocation fails the test.
 *
 * The waker starts a round only after the sleeper has returned from the
 * previous one and released the interlock, so the two do not contend on it.
 * The test depends on "lfmutex: take the fork wait_record from TLS", which
 * is why it sits after that commit and not with the TLS waiter it checks:
 * before it, a contended mutex allocates a wait_record on the kernel heap
 * under fork, and on aarch64 that happened in about one run in ten, 2-6
 * allocations from msleep() taking its sleep-queue bucket lock while another
 * kernel sleeper on the same bucket held it.  The test cannot tell those
 * from the waiter's.
 */

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-msleep-noalloc requires CONF_fork\n");
    return 0;
}

#else

#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <atomic>
#include <string>
#include <vector>
#include <osv/trace.hh>
#include <osv/sched.hh>
#include <osv/elf.hh>
#include <osv/fork_arena.hh>

#include <bsd/porting/netport.h>
#include <bsd/porting/sync_stub.h>
#include <bsd/porting/synch.h>

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

static struct mtx g_m;
static char g_chan;
static std::atomic<int> g_asleep{0};
static std::atomic<int> g_returned{0};
static const int rounds = 500;

static void *waker(void *)
{
    for (int i = 0; i < rounds; i++) {
        while (g_asleep.load() != i + 1) {
            sched::thread::yield();
        }
        mtx_lock(&g_m);   // the sleeper is inside msleep() once it released g_m
        mtx_unlock(&g_m);
        wakeup_one(&g_chan);
        while (g_returned.load() != i + 1) {
            sched::thread::yield();
        }
    }
    return nullptr;
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== tst-fork-msleep-noalloc ===\n");
    mtx_init(&g_m, "noalloc", nullptr, MTX_DEF);

    // Fork first, so every path out of main() after the probes are
    // attached detaches them.
    int pfd[2];
    if (pipe(pfd)) {
        perror("pipe");
        return 1;
    }
    pid_t child = fork();
    if (child < 0) {
        printf("FAIL: fork() failed\n");
        return 1;
    }
    if (child == 0) {
        char c;
        read(pfd[0], &c, 1);   // stay alive until the parent is done
        _exit(0);
    }

    // Every allocation on every CPU walks each tracepoint's probe list,
    // including on a CPU running the fork child's address space.  The
    // probe and the list add_probe() builds must be on the kernel heap,
    // which every address space maps the same; in this frame or in the
    // parent's heap arena the child would see something else there.
    thread_probe *probe;
    {
        fork_arena::kernel_heap_scope kh;
        probe = new thread_probe;
    }
    thread_probe &allocs = *probe;   // NB: no use after the delete below
    std::vector<tracepoint_base *> attached;
    for (auto &tp : tracepoint_base::tp_list) {
        std::string n(tp.name);
        if (n == "memory_malloc" || n == "memory_malloc_mempool" ||
            n == "memory_malloc_page" || n == "memory_malloc_large") {
            attached.push_back(&tp);
        }
    }
    if (attached.size() < 3) {
        printf("FAIL: tracepoints not found (%zu)\n", attached.size());
        write(pfd[1], "x", 1);
        waitpid(child, nullptr, 0);
        return 1;
    }
    {
        fork_arena::kernel_heap_scope kh;
        for (auto *tp : attached) {
            tp->add_probe(&allocs);
        }
    }

    pthread_t w;
    pthread_create(&w, nullptr, waker, nullptr);
    auto self = sched::thread::current();
    // woken: msleep() returned 0, which only happens when wakeup_one()
    // unlinked this thread's waiter from its bucket.  The waker calls it
    // only after the sleeper has linked the waiter and dropped g_m, so
    // every round must end that way.
    int woken = 0, timedout = 0, other = 0;
    for (int i = 0; i < rounds; i++) {
        mtx_lock(&g_m);
        g_asleep.store(i + 1);
        allocs.target.store(self);
        int r = msleep(&g_chan, &g_m, 0, "noalloc", 5000);
        allocs.target.store(nullptr);
        mtx_unlock(&g_m);
        g_returned.store(i + 1);
        woken += r == 0;
        other += r != 0;
    }
    pthread_join(w, nullptr);
    long a_woken = allocs.hits.load();
    // The timed-out path: nobody wakes these.
    for (int i = 0; i < 50; i++) {
        mtx_lock(&g_m);
        allocs.target.store(self);
        int r = msleep(&g_chan, &g_m, 0, "noalloc", 1);
        allocs.target.store(nullptr);
        mtx_unlock(&g_m);
        timedout += r == EWOULDBLOCK;
        other += r != EWOULDBLOCK;
    }

    write(pfd[1], "x", 1);
    int st;
    waitpid(child, &st, 0);
    {
        fork_arena::kernel_heap_scope kh;
        for (auto *tp : attached) {
            tp->del_probe(&allocs);
        }
    }

    // del_probe() waited for an RCU grace period, so no CPU is in hit().
    long a_timedout = allocs.hits.load() - a_woken;
    {
        fork_arena::kernel_heap_scope kh;
        delete probe;
    }
    printf("msleep() with a live fork child: %d woken by wakeup_one() (must be %d), "
           "%d timed out (must be 50), %d other results (must be 0)\n",
           woken, rounds, timedout, other);
    printf("allocations inside msleep(): %ld woken, %ld timed out (must be 0 and 0)\n",
           a_woken, a_timedout);
    if (woken != rounds || timedout != 50 || other != 0) {
        printf("FAIL: not every sleep ended as expected\n");
        printf("SUMMARY: FAIL\n");
        return 1;
    }
    if (a_woken || a_timedout) {
        printf("FAIL: msleep() allocates\n");
        printf("SUMMARY: FAIL\n");
        return 1;
    }
    printf("SUMMARY: 0 failures\n");
    return 0;
}

#endif
