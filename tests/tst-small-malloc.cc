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

    const int allocation_count = 1024;
    for( int i = 0; i < allocation_count; i++) {
        void *addr = malloc(7);
        assert(addr);
        free(addr);

        addr = malloc(17);
        assert(addr);
        free(addr);
    }

    // Verify all allocations above were handled by malloc_pool
    assert(memory_malloc_mempool_counter->read() - memory_malloc_mempool_counter_now >= 2 * allocation_count);
    // Verify that NO allocations were handled by alloc_page
    assert(memory_malloc_page_counter->read() - memory_malloc_page_counter_now == 0);
}