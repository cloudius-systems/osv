/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A forked process makes a 128 KiB kernel-heap allocation, as ZFS's
// zio_buf_alloc() does, while the linear map is fragmented.  Under CONF_fork
// such an allocation must come from the linear map and waits for it rather
// than fall back to map_anon().  With its header page it is 33 pages, so the
// page-range allocator looks for a free range of 64 pages or more.  If it
// never looks at the 32-63 page list, then once no range of 64 pages is left
// the caller waits forever while the reclaimer keeps waking it, although
// plenty of memory sits in ranges of 33 pages or more.
//
// The parent takes every free range of 64 pages or more, 64 pages at a time,
// keeps the first and last page of each block and frees the 62 in between, so
// only ranges of 62 pages or less stay free.  It keeps doing that while the
// child allocates, and fails if the child has no buffer within the time limit
// (argv[1] seconds, default 10) or gets one outside the linear map.  The two
// processes talk through a MAP_SHARED page, so that nothing but the allocation
// under test has to work across fork.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-large-alloc-frag requires CONF_fork\n");
    return 0;
}

#else

#include <osv/fork_arena.hh>
#include <osv/mempool.hh>
#include <osv/pagealloc.hh>
#include <osv/contiguous_alloc.hh>
#include <osv/mmu-defs.hh>
#include <osv/ilog2.hh>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <vector>

static constexpr size_t page = mmu::page_size;
static constexpr size_t block_pages = 64;
static std::vector<char*> blocks;

struct handshake {
    std::atomic<int> go;
    std::atomic<int> result;   // 0 until the child is done, then 'L', 'M' or 'N'
};
static handshake* hs;

// Take one free range of 64 pages or more, keep its first and last page and
// free the 62 in between.  Returns false when no such range is left.
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

// Free ranges of 2^lo .. 2^hi - 1 pages (hi = page_ranges_max_order + 1
// includes the huge list).
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

static bool in_linear_map(void* p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= mmu::main_mem_area_base &&
           a < mmu::main_mem_area_base + mmu::mem_area_size;
}

int main(int argc, char** argv)
{
    int limit_s = argc > 1 ? atoi(argv[1]) : 10;
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
        void* buf = nullptr;
        int err;
        {
            fork_arena::kernel_heap_scope kh;
            err = posix_memalign(&buf, page, 128 * 1024);
        }
        char r = (err || !buf) ? 'N' : in_linear_map(buf) ? 'L' : 'M';
        free(buf);
        hs->result.store(r);
        _exit(0);
    }

    blocks.reserve(memory::stats::total() / (block_pages * page) + 16);
    unsigned order = ilog2(block_pages);
    size_t big = 1;
    for (int i = 0; i < 100 && big; i++) {
        carve_all();
        big = free_ranges(order, memory::page_ranges_max_order + 1);
    }
    size_t mid = free_ranges(order - 1, order);
    printf("carved %zu blocks: %zu free ranges of 32-63 pages, %zu of 64+\n",
           blocks.size(), mid, big);
    if (big || !mid) {
        printf("FAIL: could not fragment the linear map\n");
        release_all();
        return 1;
    }

    auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    hs->go.store(1);
    char r = 0;
    while (!r && elapsed() < limit_s) {
        carve_all();
        usleep(1000);
        r = hs->result.load();
    }
    bool got = r != 0;
    double secs = elapsed();
    size_t nblocks = blocks.size();
    if (!got) {
        printf("FAIL: child's 128 KiB kernel-heap allocation still waiting after %.1f s "
               "(%zu blocks held, %zu ranges of 64+ pages free)\n", secs, nblocks,
               free_ranges(order, memory::page_ranges_max_order + 1));
    }
    release_all();
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status)) {
        printf("FAIL: child status 0x%x\n", status);
        return 1;
    }
    if (!got) {
        return 1;
    }
    if (r != 'L') {
        printf("FAIL: child's allocation %s\n",
               r == 'N' ? "failed" : "is outside the linear map");
        return 1;
    }
    printf("child got a 128 KiB linear-map buffer in %.3f s with %zu blocks held\nPASS\n",
           secs, nblocks);
    return 0;
}

#endif
