/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A freed stack-window range must not stay reachable through another CPU's
// TLB.  stack_window::free() clears the range's page-table entries, flushes
// every CPU's TLB, frees the pages and only then lets the VA be handed out
// again.  Without the flush, a CPU that touched the old stack keeps the old
// translation: after the VA is reused, a write from that CPU lands in the old,
// freed page instead of the new one.
//
// A kernel thread pinned to CPU 1 writes every page of a window range, then
// spins without switching address spaces, so its TLB keeps the translations.
// A kernel thread pinned to CPU 0 frees the range and allocates the same size
// again, which returns the same VA with new pages.  The CPU 1 thread then
// writes a marker through the VA, and the CPU 0 thread must read it there.

#include <osv/kernel_config_fork.h>
#include <osv/kernel_config_memory_debug.h>

#include <cstdio>

#if !CONF_fork || CONF_memory_debug

int main()
{
    printf("SKIP: tst-fork-window-tlb requires CONF_fork without CONF_memory_debug\n");
    return 0;
}

#else

#include <osv/sched.hh>
#include <osv/mmu-defs.hh>
#include <osv/stack_window.hh>
#include <unistd.h>
#include <atomic>
#include <cstdint>

static constexpr size_t page = mmu::page_size;
// A size no kernel stack or thread object uses, so the test's ranges are its own.
static constexpr size_t bytes = 5 * page;
static constexpr int rounds = 50;

static std::atomic<int> step;
static std::atomic<char*> range;
static std::atomic<long> marker;
static std::atomic<bool> finished;
static std::atomic<int> started;

static void touch_and_write()
{
    started++;
    for (int r = 0; r < rounds; r++) {
        while (step.load() != 1 + 4 * r) {
        }
        auto p = range.load();
        for (size_t off = 0; off < bytes; off += page) {
            *reinterpret_cast<volatile long*>(p + off) = 1;
        }
        step.store(2 + 4 * r);
        while (step.load() != 3 + 4 * r) {
        }
        *reinterpret_cast<volatile long*>(p + 2 * page + 64) = marker.load();
        step.store(4 + 4 * r);
    }
}

static int stale, reused;

static void free_and_reuse()
{
    started++;
    for (int r = 0; r < rounds; r++) {
        auto p = static_cast<char*>(mmu::stack_window::alloc(bytes));
        range.store(p);
        step.store(1 + 4 * r);
        while (step.load() != 2 + 4 * r) {
        }
        mmu::stack_window::free(p, bytes);
        auto q = static_cast<char*>(mmu::stack_window::alloc(bytes));
        reused += q == p;
        long m = 0x77a10000L + r;
        marker.store(m);
        step.store(3 + 4 * r);
        while (step.load() != 4 + 4 * r) {
        }
        if (*reinterpret_cast<volatile long*>(q + 2 * page + 64) != m) {
            stale++;
        }
        mmu::stack_window::free(q, bytes);
    }
    finished.store(true);
}

int main()
{
    if (sched::cpus.size() < 2) {
        printf("SKIP: tst-fork-window-tlb needs 2 CPUs\n");
        return 0;
    }
    auto probe = mmu::stack_window::alloc(bytes);
    if (!probe) {
        printf("SKIP: no stack window\n");
        return 0;
    }
    mmu::stack_window::free(probe, bytes);
    // Both sides are kernel threads pinned to their CPUs, so the writer's CPU
    // keeps running the same address space and nothing else flushes its TLB.
    auto w = sched::thread::make(touch_and_write,
                                 sched::thread::attr().pin(sched::cpus[1]));
    auto f = sched::thread::make(free_and_reuse,
                                 sched::thread::attr().pin(sched::cpus[0]));
    w->start();
    f->start();
    for (int i = 0; i < 2000 && !finished.load(); i++) {
        usleep(10000);
    }
    if (!finished.load()) {
        printf("FAIL: stuck at step %d after 20 s, %d of 2 threads started\n",
               step.load(), started.load());
        return 1;
    }
    f->join();
    w->join();
    delete f;
    delete w;
    printf("%d rounds, %d reused the freed VA, %d writes from CPU 1 missed the new page\n",
           rounds, reused, stale);
    if (reused != rounds) {
        printf("FAIL: the window did not reuse the freed VA, so this did not test it\n");
        return 1;
    }
    if (stale) {
        printf("FAIL: another CPU kept a freed window page's translation\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}

#endif
