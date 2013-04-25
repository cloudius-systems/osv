#include <string.h>
#include <stdlib.h>
#include <execinfo.h>

#include "alloctracker.hh"

namespace memory {

void alloc_tracker::remember(void *addr, int size)
{
    std::lock_guard<mutex> guard(lock);
    // If the code in this method causes new allocations, they will in turn
    // cause a nested call to this function. To avoid endless recursion, we
    // do not track these deeper memory allocations. Remember that the
    // purpose of the allocation tracking is finding memory leaks in the
    // application code, not in the code in this file.
    if (lock.getdepth() > 1) {
        return;
    }

    struct alloc_info *a = nullptr;
    for (size_t i = 0; i < size_allocations; i++) {
        if(!allocations[i].addr){
            // found a free spot, reuse it
            a = &allocations[i];
         }
    }
    if (!a) {
        // Grow the vector to make room for more allocation records.
        int old_size = size_allocations;
        size_allocations = size_allocations ? 2*size_allocations : 1024;
        struct alloc_info *old_allocations = allocations;
        allocations = (struct alloc_info *) malloc(
                size_allocations * sizeof(struct alloc_info));
        if (old_allocations)
            memcpy(allocations, old_allocations,
                size_allocations * sizeof(struct alloc_info));
        for (size_t i = old_size; i < size_allocations; i++)
            allocations[i].addr=0;
        a = &allocations[old_size];
    }

    a->seq = current_seq++;
    a->addr = addr;
    a->size = size;

    // Do the backtrace. If we ask for only a small number of call levels
    // we'll get only the deepest (most recent) levels, but we are more
    // interested in the highest level functions, so we ask for 1024 levels
    // (assuming we'll never have deeper recursion than that), and later only
    // save the highest levels.
    static void *bt[1024];
    int n = backtrace(bt, sizeof(bt)/sizeof(*bt));

    // When backtrace is too deep, save only the MAX_BACKTRACE most high
    // level functions (not the first MAX_BACKTRACE functions!).
    // We can ignore two functions at the start (always remember() itself
    // and then malloc/alloc_page) and and at the end (typically some assembly
    // code or ununderstood address).
    static constexpr int N_USELESS_FUNCS_START = 2;
    static constexpr int N_USELESS_FUNCS_END = 1;
    void **bt_from = bt + N_USELESS_FUNCS_START;
    n -= N_USELESS_FUNCS_START+N_USELESS_FUNCS_END;
    if(n > MAX_BACKTRACE) {
        bt_from += n - MAX_BACKTRACE;
        n = MAX_BACKTRACE;
    }
    a->nbacktrace = n < 0 ? 0 : n;
    for (int i = 0; i < n; i++) {
        a->backtrace[i] = bt_from[i];
    }
}

void alloc_tracker::forget(void *addr)
{
    if (!addr)
        return;
    std::lock_guard<mutex> guard(lock);
    for (size_t i = 0; i < size_allocations; i++){
        if (allocations[i].addr == addr) {
            allocations[i].addr = 0;
            return;
        }
    }
}

}
