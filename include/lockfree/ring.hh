/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

//
// single-producer / single-consumer lockless ring buffer of fixed size.
//
#ifndef __LF_RING_HH__
#define __LF_RING_HH__

#include <atomic>
#include <osv/sched.hh>
#include <arch.hh>
#include <osv/ilog2.hh>

//
// spsc ring of fixed size
//
template<class T, unsigned MaxSize, unsigned MaxSizeMask = MaxSize - 1>
class ring_spsc {
public:
    ring_spsc(): _begin(0), _end(0) { assert(is_power_of_two(MaxSize)); }

    bool push(const T& element)
    {
        unsigned end = _end.load(std::memory_order_relaxed);

        if (size() >= MaxSize) {
            return false;
        }

        _ring[end & MaxSizeMask] = element;
        _end.store(end + 1, std::memory_order_release);

        return true;
    }

    bool pop(T& element)
    {
        unsigned beg = _begin.load(std::memory_order_relaxed);
        unsigned end = _end.load(std::memory_order_acquire);

        if (beg == end) {
            return false;
        }

        element = _ring[beg & MaxSizeMask];
        _begin.store(beg + 1, std::memory_order_relaxed);

        return true;
    }

    /**
     * Should be called by the producer. When called by the consumer may
     * someties return a smaller value than the actual elements count.
     *
     * @return the current number of the elements.
     */
    unsigned size() const {
        unsigned end = _end.load(std::memory_order_relaxed);
        unsigned beg = _begin.load(std::memory_order_relaxed);

        return (end - beg);
    }

private:
    std::atomic<unsigned> _begin CACHELINE_ALIGNED;
    std::atomic<unsigned> _end CACHELINE_ALIGNED;
    T _ring[MaxSize];
};

#endif // !__LF_RING_HH__
