/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// alloc_tracker is used to track living allocations, and the call chain which
// lead to each allocation. This is useful for leak detection.
//
// alloc_tracker implements only two methods: remember(addr, size), to
// remember a new allocation, and forget(addr), to forget a previous
// allocation. There is currently no programmatic API for querying this
// data structure from within OSV. Instead, our gdb python extension (see
// "loader.py") will find the memory::tracker object, inspect it, and report
// the suspected leaks and/or other statistics about the leaving allocations.
#ifndef INCLUDED_ALLOCTRACKER_H
#define INCLUDED_ALLOCTRACKER_H
#include <osv/mutex.h>
#include <cstdint>

namespace memory {

class alloc_tracker {
public:
    void remember(void *addr, int size);
    void forget(void *addr);
private:
    // For each allocation we remember MAX_BACKTRACE functions (or rather,
    // instruction pointers) on the call chain. We remember either the
    // deepest (newest) calls, if POLICY_DEEPEST, else the shallowest
    // (top-level calls).
    static constexpr int MAX_BACKTRACE = 20;
    static constexpr bool POLICY_DEEPEST = true;

    // For simplicity, we hold the list of living allocations in a simple
    // array of alloc_info node - not a more sophisticated hash table.
    // It sounds like this has terrible O(N) performance, but we usually
    // get great performance thanks to two tricks:
    // 1. We keep the unused nodes linked together (see next and first_free)
    //    so getting a free node for rememeber() is always O(1).
    // 2. We keep the used nodes linked together (again next, and
    //    newest_allocation) from newest to oldest, so forget() can always
    //    start with the newest allocation - so the popular case of free()
    //    quickly after malloc() is very fast.
    // Benchmarks show that these two tricks significantly (up to 3-fold)
    // increase performance of leak-checked program, and provides exactly
    // the same performance as a real O(1) data structure (like hash table).

    struct alloc_info {
        // sequential number of allocation (to know how "old" this allocation
        // is):
        unsigned int seq;
        // number of bytes allocated:
        unsigned int size;
        // allocation's address (addr==0 is a deleted item)
        void *addr;
        // the backtrace - MAX_BACKTRACE highest-level functions in the call
        // chain that led to this allocation.
        int nbacktrace;
        void *backtrace[MAX_BACKTRACE];
        // "next" is the index of the next alloc_info node. For in-use nodes,
        // this points to the next older allocation, and for free nodes, it
        // points to the next node on the free list. The value -1 signifies
        // a nonexistant node.
        int next;
    };

    // Indexes of the first free node, and the most recently used mode.
    // Each is the head of a list, using the "next" pointer and ending in -1.
    int first_free;
    int newest_allocation;

    struct alloc_info *allocations = 0;
    size_t size_allocations = 0;
    unsigned long current_seq = 0;
    mutex lock;

    // If the allocation tracking code causes new allocations, they will in
    // turn cause a nested call to the tracking functions. To avoid endless
    // recursion, we do not track these deeper memory allocations. Remember
    // that the purpose of the allocation tracking is finding memory leaks in
    // the application code, not allocation tracker code.
    class in_tracker_t {
    private:
        bool flag;
    public:
        void lock() {
            flag = true;
        }
        void unlock() {
            flag = false;
        }
        operator bool() {
            return flag;
        }
    };
    static __thread class in_tracker_t in_tracker;

};
#endif

}
