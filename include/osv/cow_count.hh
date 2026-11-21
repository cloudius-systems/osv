/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_COW_COUNT_HH
#define OSV_COW_COUNT_HH

#include <atomic>
#include <cstdint>

namespace mmu {

// The count fork keeps for a copy-on-write frame: how many address spaces map
// it beyond the first.  The frame is freed only by whoever drops the count
// while it is zero (cow_count_drop() returns false), so a count that is too
// high can keep a frame allocated but can never free one still mapped.
//
// The count saturates.  Some mappings are counted and never dropped (fork
// counts each child's shared mapping of a user-supplied thread stack), so a
// process that forks for months can add more than 2^32 of them to one frame.
// A plain 32-bit counter would wrap to zero, and the next drop would free a
// frame other address spaces still map.  Once the count reaches the maximum
// it stays there: neither add nor drop changes it again, and drop always
// reports another mapper.  The frame is then never freed through the count,
// which loses one page for good but never frees one in use.
constexpr uint32_t cow_count_max = UINT32_MAX;

inline void cow_count_add(std::atomic<uint32_t> &c)
{
    uint32_t v = c.load(std::memory_order_acquire);
    while (v != cow_count_max &&
           !c.compare_exchange_weak(v, v + 1, std::memory_order_acq_rel)) {
    }
}

// Drop one mapper.  Returns true if another address space still maps the
// frame, false if the caller was the last and now owns it.
inline bool cow_count_drop(std::atomic<uint32_t> &c)
{
    uint32_t v = c.load(std::memory_order_acquire);
    while (v != 0 && v != cow_count_max &&
           !c.compare_exchange_weak(v, v - 1, std::memory_order_acq_rel)) {
    }
    return v != 0;
}

}

#endif
