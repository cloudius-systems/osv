/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Under CONF_fork every large kernel-heap allocation must be one physically
// contiguous range of the linear map.  A page-aligned 128 KiB buffer, the
// size ZFS allocates for every zio buffer, must then take exactly 32 pages:
// a 33rd page for a header makes it an order-6 request and leaves 31-page
// remainders behind, which is what fragments the linear map under load.
//
// The test makes 256 such allocations under kernel_heap_scope and measures
// the free memory they take, checks that each one is writable over its whole
// size and reports that size, that realloc() keeps its contents, and that
// freeing them gives all the memory back.  Last, it checks that a headered
// object allocated over a freed headerless buffer's pages keeps its own size.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-kheap-headerless requires CONF_fork\n");
    return 0;
}

#else

#include <osv/fork_arena.hh>
#include <osv/mempool.hh>
#include <osv/pagealloc.hh>
#include <osv/contiguous_alloc.hh>
#include <malloc.h>
#include <cstdlib>
#include <cstring>
#include <vector>

static constexpr size_t page = memory::page_size;
static constexpr size_t buf_size = 128 * 1024;
static constexpr int nbufs = 256;

static void* alloc_headerless(size_t size)
{
    fork_arena::kernel_heap_scope kh;
    void* p = nullptr;
    return posix_memalign(&p, page, size) ? nullptr : p;
}

// Free a headerless buffer, then allocate a headered page-aligned object
// (alloc_phys_contiguous_aligned puts its header in the page before the
// object) that starts at the same address.  If the buffer's size-table entry
// had been left behind, the object would read as headerless: the wrong size,
// and a free of the wrong pages.  The pages are placed by hand: a block of 65
// pages is held, its pages 2..33 are freed as one 32-page range (the newest of
// its order, which the next 32-page request takes), and page 1 is freed just
// before the headered request, so its header lands on page 1 and the object
// on page 2.  Also frees a headerless buffer with free_phys_contiguous_aligned.
static bool reuse_by_headered()
{
    constexpr size_t hl_pages = buf_size / page;      // 32
    constexpr size_t hd_pages = hl_pages - 1;         // 31, plus a header page
    constexpr size_t blk_pages = 65;
    for (int attempt = 1; attempt <= 20; attempt++) {
        long before = memory::stats::free();
        auto blk = static_cast<char*>(memory::alloc_phys_contiguous_aligned(
            (blk_pages - 1) * page, page)) - page;
        char* x = blk + 2 * page;
        char* tail = x + hl_pages * page;
        auto release = [&] {
            memory::free_huge_page(blk, page);
            memory::free_huge_page(tail, (blk_pages - 2 - hl_pages) * page);
        };
        memory::free_huge_page(x, hl_pages * page);
        void* hl = alloc_headerless(buf_size);
        if (hl != x) {
            // Another allocation took the range first.
            free(hl);
            memory::free_huge_page(x - page, page);
            release();
            continue;
        }
        free(hl);
        memory::free_huge_page(x - page, page);
        auto hd = memory::alloc_phys_contiguous_aligned(hd_pages * page, page);
        if (hd != x) {
            memory::free_phys_contiguous_aligned(hd);
            release();
            continue;
        }
        size_t got = malloc_usable_size(hd);
        printf("a %zu-page headered object at a freed %zu-page headerless "
               "buffer reads %zu pages (attempt %d)\n", hd_pages, hl_pages,
               got / page, attempt);
        if (got != hd_pages * page) {
            // Freeing it now would return the wrong pages.
            printf("FAIL: a freed headerless buffer's size entry was left behind\n");
            return false;
        }
        memory::free_phys_contiguous_aligned(hd);
        release();
        void* hl2 = alloc_headerless(buf_size);
        if (!hl2) {
            printf("FAIL: posix_memalign\n");
            return false;
        }
        memory::free_phys_contiguous_aligned(hl2);
        long left = before - static_cast<long>(memory::stats::free());
        printf("%ld KiB not returned after freeing them\n", left / 1024);
        if (left > 64 * 1024 || left < -64 * 1024) {
            printf("FAIL: freeing the reused pages did not give the memory back\n");
            return false;
        }
        return true;
    }
    printf("FAIL: could not place the headered object over the freed buffer\n");
    return false;
}

int main()
{
    std::vector<void*> bufs(nbufs);
    bool ok = true;
    long before = memory::stats::free();
    {
        fork_arena::kernel_heap_scope kh;
        for (auto& b : bufs) {
            if (posix_memalign(&b, page, buf_size)) {
                printf("FAIL: posix_memalign\n");
                return 1;
            }
        }
    }
    long taken = before - static_cast<long>(memory::stats::free());
    double pages = static_cast<double>(taken) / page / nbufs;
    for (auto b : bufs) {
        memset(b, 0xa5, buf_size);
        ok &= malloc_usable_size(b) == buf_size;
    }
    void* r;
    {
        fork_arena::kernel_heap_scope kh;
        r = realloc(bufs[0], 2 * buf_size);
    }
    if (r) {
        bufs[0] = r;
        auto c = static_cast<unsigned char*>(r);
        for (size_t i = 0; i < buf_size; i++) {
            ok &= c[i] == 0xa5;
        }
        ok &= malloc_usable_size(r) >= 2 * buf_size;
    }
    for (auto b : bufs) {
        free(b);
    }
    long left = before - static_cast<long>(memory::stats::free());
    printf("%d page-aligned 128 KiB kernel-heap buffers took %.2f pages each; "
           "%ld KiB not returned after free\n", nbufs, pages, left / 1024);
    if (!r || !ok) {
        printf("FAIL: a buffer's size or contents are wrong\n");
        return 1;
    }
    if (pages > 32.5 || pages < 31.5) {
        printf("FAIL: want 32 pages per buffer\n");
        return 1;
    }
    if (left > 64 * 1024 || left < -64 * 1024) {
        printf("FAIL: free() did not give the memory back\n");
        return 1;
    }
    if (!reuse_by_headered()) {
        return 1;
    }
    printf("PASS\n");
    return 0;
}

#endif
