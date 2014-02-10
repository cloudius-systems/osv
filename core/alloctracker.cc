/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <string.h>
#include <stdlib.h>
#include <osv/execinfo.hh>

#include <osv/alloctracker.hh>

namespace memory {

__thread alloc_tracker::in_tracker_t alloc_tracker::in_tracker;

void alloc_tracker::remember(void *addr, int size)
{
    if (in_tracker)
        return;
    std::lock_guard<in_tracker_t> intracker(in_tracker);

    int index;

    WITH_LOCK(lock) {
        if (!allocations || first_free < 0) {
            // Grow the vector to make room for more allocation records.
            int old_size = size_allocations;
            size_allocations = size_allocations ? 2*size_allocations : 1024;
            struct alloc_info *old_allocations = allocations;
            allocations = (struct alloc_info *)
                         malloc(size_allocations * sizeof(struct alloc_info));
            if (old_allocations) {
                memcpy(allocations, old_allocations,
                        size_allocations * sizeof(struct alloc_info));
            } else {
                first_free = -1;
                newest_allocation = -1;
            }
            for (size_t i = old_size; i < size_allocations; ++i) {
                allocations[i].addr = 0;
                allocations[i].next = first_free;
                first_free = i;
            }
        }

        // Free node available, reuse it
        index = first_free;
        struct alloc_info *a = &allocations[index];
        first_free = a->next;
        a->next = newest_allocation;
        newest_allocation = index;

        a->seq = current_seq++;
        a->addr = addr;
        a->size = size;
    }

    // Do the backtrace. If we ask for only a small number of call levels
    // we'll get only the deepest (most recent) levels, but when
    // !POLICY_DEEPEST we are more  interested in the highest level functions,
    // so we ask for 1024 levels (assuming we'll never have deeper recursion than
    // that), and later only save the highest levels.
    static void *bt[POLICY_DEEPEST ? MAX_BACKTRACE : 1024];
    // We don't want to trigger a demand page fault, since this allocation may
    // be servicing a fault itself.  Use backtrace_safe().
    int n = backtrace_safe(bt, sizeof(bt)/sizeof(*bt));

    // When backtrace is too deep, save only the MAX_BACKTRACE most high
    // level functions (not the first MAX_BACKTRACE functions!).
    // We can ignore two functions at the start (always remember() itself
    // and then malloc/alloc_page) and and at the end (typically some assembly
    // code or ununderstood address).
    static constexpr int N_USELESS_FUNCS_START = 2;
    static constexpr int N_USELESS_FUNCS_END = 0;
    void **bt_from = bt + N_USELESS_FUNCS_START;
    n -= N_USELESS_FUNCS_START+N_USELESS_FUNCS_END;
    if(n > MAX_BACKTRACE) {
        bt_from += n - MAX_BACKTRACE;
        n = MAX_BACKTRACE;
    }

    // We need the lock back, to prevent a concurrent allocation from moving
    // allocations[] while we use it.
    WITH_LOCK(lock) {
        struct alloc_info *a = &allocations[index];
        a->nbacktrace = n < 0 ? 0 : n;
        for (int i = 0; i < n; i++) {
            a->backtrace[i] = bt_from[i];
        }
    }
}

void alloc_tracker::forget(void *addr)
{
    if (!addr)
        return;
    if (in_tracker)
        return;
    std::lock_guard<in_tracker_t> intracker(in_tracker);
    std::lock_guard<mutex> guard(lock);
    if (!allocations) {
        return;
    }

    for (int *i = &newest_allocation; *i >= 0; i = &(allocations[*i].next)) {
        if (allocations[*i].addr == addr) {
            allocations[*i].addr = 0;
            int save_next_allocation = allocations[*i].next;
            allocations[*i].next = first_free;
            first_free = *i;
            *i = save_next_allocation;
            return;
        }
    }
}

}
