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
    static constexpr int MAX_BACKTRACE = 20;
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
    };

    // The current implementation for searching allocated blocks and remembering
    // new ones is with a slow linear search. This is very slow when there are
    // a lot of living allocations. It should be changed to a hash table!
    struct alloc_info *allocations = 0;
    size_t size_allocations = 0;
    unsigned long current_seq = 0;
    mutex lock;
};
#endif

}
