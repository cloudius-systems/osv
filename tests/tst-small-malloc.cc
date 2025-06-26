/*
 * Copyright (C) 2018 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <cassert>
#include <osv/trace-count.hh>

// Since gcc 15, there is a new optimization ("builtin-malloc") where the
// compiler can see that the return value of malloc() isn't saved, and
// optimize the call away! Since our goal is for these malloc()s to happen
// (and count their tracepoints), we need to save their return values
// somewhere. A single volatile variable is enough to avoid the optimization.
volatile void* dont_optimize;

void test_malloc(size_t size) {
    void *addr = malloc(size);
    assert(addr);
    assert(reinterpret_cast<uintptr_t>(addr) % 8 == 0);
    dont_optimize = addr;
}

void test_aligned_alloc(size_t alignment, size_t size) {
    void *addr = aligned_alloc(alignment, size);
    assert(addr);
    assert(reinterpret_cast<uintptr_t>(addr) % alignment == 0);
    dont_optimize = addr;
}

int main() {
    tracepoint_counter *memory_malloc_mempool_counter = nullptr,
            *memory_malloc_page_counter = nullptr;

    for (auto & tp : tracepoint_base::tp_list) {
        if ("memory_malloc_mempool" == std::string(tp.name)) {
            memory_malloc_mempool_counter = new tracepoint_counter(tp);
        }
        if ("memory_malloc_page" == std::string(tp.name)) {
            memory_malloc_page_counter = new tracepoint_counter(tp);
        }
    }
    assert(memory_malloc_mempool_counter != nullptr);
    assert(memory_malloc_page_counter != nullptr);

    auto memory_malloc_mempool_counter_now = memory_malloc_mempool_counter->read();
    auto memory_malloc_page_counter_now = memory_malloc_page_counter->read();

    const int allocation_count = 256;
    for( int i = 0; i < allocation_count; i++) {
        // Expects malloc_pool allocations
        test_malloc(3);
        test_malloc(4);
        test_malloc(6);
        test_malloc(7);
        test_malloc(8);
        test_malloc(9);
        test_malloc(15);
        test_malloc(16);
        test_malloc(17);
        test_malloc(32);
        test_malloc(1024);

        // Expects malloc_pool allocations
        test_aligned_alloc(16, 5);
        test_aligned_alloc(16, 19);
        test_aligned_alloc(32, 17);
        test_aligned_alloc(1024, 255);

        // Expects full page allocations
        test_malloc(1025);
        test_aligned_alloc(2048, 1027);
    }

    // Verify correct number of allocations above were handled by malloc_pool
    assert(memory_malloc_mempool_counter->read() - memory_malloc_mempool_counter_now >= 15 * allocation_count);

    // Verify correct number of allocations were handled by alloc_page
    assert(memory_malloc_page_counter->read() - memory_malloc_page_counter_now == 2 * allocation_count);
}
