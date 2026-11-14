/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A forked process creates kernel threads while the linear map is fragmented.
// Under CONF_fork a kernel stack must be mapped the same way in every address
// space, so it cannot come from the process's own copy-on-write memory.  If it
// can only come from a free physically contiguous range of the linear map,
// then once no range of 17 pages (a 64 KiB stack) or 65 pages (256 KiB, the
// size the BSD and ZFS kernel threads use) is left, thread creation waits
// forever while plenty of memory is free in smaller ranges.
//
// The parent takes every free range of 16 pages or more, 16 pages at a time,
// keeps the first and last page of each block and frees the 14 in between, so
// only ranges of 14 pages or less stay free.  It keeps doing that while the
// child starts, runs and joins kernel threads with both stack sizes, and fails
// if the child is not done within the time limit (argv[1] seconds, default 20).
// The child also checks that each stack can be written and read back through
// the thread, and the parent reads a word the child's thread wrote, through a
// MAP_SHARED page, after the child exits.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-stack-frag requires CONF_fork\n");
    return 0;
}

#else

#include <osv/mempool.hh>
#include <osv/pagealloc.hh>
#include <osv/contiguous_alloc.hh>
#include <osv/mmu-defs.hh>
#include <osv/ilog2.hh>
#include <osv/sched.hh>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <vector>

static constexpr size_t page = mmu::page_size;
static constexpr size_t block_pages = 16;
static constexpr int rounds = 20;
static std::vector<char*> blocks;

struct handshake {
    std::atomic<int> go;
    std::atomic<int> done;      // 0 until the child is done, then 1 ok, 2 bad stack
    std::atomic<long> marker;   // written by the child's kernel threads
};
static handshake* hs;

static bool carve_one()
{
    if (blocks.size() == blocks.capacity()) {
        return false;
    }
    void* p = memory::alloc_phys_contiguous_aligned((block_pages - 1) * page,
                                                    page, false);
    if (!p) {
        return false;
    }
    char* start = static_cast<char*>(p) - page;   // the allocator's header page
    blocks.push_back(start);
    memory::free_huge_page(start + page, (block_pages - 2) * page);
    return true;
}

static void carve_all()
{
    while (carve_one()) {
    }
}

static void release_all()
{
    for (auto b : blocks) {
        memory::free_huge_page(b, page);
        memory::free_huge_page(b + (block_pages - 1) * page, page);
    }
    blocks.clear();
}

static size_t free_ranges(unsigned lo, unsigned hi)
{
    memory::stats::page_ranges_stats st;
    memory::stats::get_page_ranges_stats(st);
    size_t n = 0;
    for (unsigned o = lo; o < hi; o++) {
        n += st.order[o].ranges_num;
    }
    return n;
}

// Run one kernel thread with a stack of `bytes`.  It writes a pattern across
// most of its stack and reads it back, so a stack that is not fully mapped or
// not private to the thread is caught.
static bool run_thread(size_t bytes, long tag)
{
    std::atomic<bool> ok{false};
    auto t = sched::thread::make([&] {
        constexpr size_t n = 4 * 1024;   // 32 KiB, half the smaller stack
        volatile long buf[n];
        for (size_t i = 0; i < n; i++) {
            buf[i] = tag ^ static_cast<long>(i);
        }
        bool good = true;
        for (size_t i = 0; i < n; i++) {
            good &= buf[i] == (tag ^ static_cast<long>(i));
        }
        hs->marker.store(tag);
        ok.store(good);
    }, sched::thread::attr().stack(bytes).name("stack-frag"));
    t->start();
    t->join();
    delete t;
    return ok.load();
}

int main(int argc, char** argv)
{
    int limit_s = argc > 1 ? atoi(argv[1]) : 20;
    void* shm = mmap(nullptr, page, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shm == MAP_FAILED) {
        printf("FAIL: mmap\n");
        return 1;
    }
    hs = new (shm) handshake{};
    // Fork before fragmenting, so fork() itself never needs a large range.
    pid_t pid = fork();
    if (pid < 0) {
        printf("FAIL: fork\n");
        return 1;
    }
    if (pid == 0) {
        while (!hs->go.load()) {
            usleep(1000);
        }
        bool good = true;
        for (int i = 0; i < rounds && good; i++) {
            good &= run_thread(64 * 1024, 0x6400 + i);
            good &= run_thread(256 * 1024, 0x25600 + i);
        }
        hs->done.store(good ? 1 : 2);
        _exit(0);
    }

    blocks.reserve(memory::stats::total() / (block_pages * page) + 16);
    unsigned order = ilog2(block_pages);
    size_t big = 1;
    for (int i = 0; i < 100 && big; i++) {
        carve_all();
        big = free_ranges(order, memory::page_ranges_max_order + 1);
    }
    printf("carved %zu blocks: %zu free ranges of 16+ pages left\n",
           blocks.size(), big);
    if (big) {
        printf("FAIL: could not fragment the linear map\n");
        release_all();
        return 1;
    }

    auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    hs->go.store(1);
    int done = 0;
    while (!done && elapsed() < limit_s) {
        carve_all();
        usleep(1000);
        done = hs->done.load();
    }
    double secs = elapsed();
    if (!done) {
        printf("FAIL: child's kernel threads still not started after %.1f s "
               "(%zu blocks held, %zu ranges of 16+ pages free)\n", secs,
               blocks.size(), free_ranges(order, memory::page_ranges_max_order + 1));
    }
    release_all();
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status)) {
        printf("FAIL: child status 0x%x\n", status);
        return 1;
    }
    if (!done) {
        return 1;
    }
    if (done != 1) {
        printf("FAIL: a kernel thread's stack did not read back what it wrote\n");
        return 1;
    }
    if (hs->marker.load() != 0x25600 + rounds - 1) {
        printf("FAIL: the parent does not see the child's thread's write\n");
        return 1;
    }
    printf("%d rounds of 64 KiB and 256 KiB kernel threads in a forked child in %.3f s\nPASS\n",
           rounds, secs);
    return 0;
}

#endif
