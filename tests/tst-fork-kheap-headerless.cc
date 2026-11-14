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
// freeing them gives all the memory back.

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
#include <malloc.h>
#include <cstdlib>
#include <cstring>
#include <vector>

static constexpr size_t page = memory::page_size;
static constexpr size_t buf_size = 128 * 1024;
static constexpr int nbufs = 256;

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
    printf("PASS\n");
    return 0;
}

#endif
