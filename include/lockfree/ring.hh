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
#include <osv/debug.hh>

//
// spsc ring of fixed size
//
template<class T, unsigned MaxSize, unsigned MaxSizeMask = MaxSize - 1>
class ring_spsc {
public:
    ring_spsc(): _begin(0), _end(0)
    {
        static_assert(is_power_of_two(MaxSize), "size must be a power of two");
    }

    bool push(const T& element)
    {
        unsigned end = _end.load(std::memory_order_relaxed);
        //
        // Use "acquire" memory order to prevent the reordering of this load and
        // the "_ring[end & MaxSizeMask] = element" store below since otherwise
        // the following race is possible on a platform that may perform
        // speculative stores:
        //
        // Initial indices values: _begin = 0, _end = MaxSize.
        //
        // 1) [CPU0]: load _end (end <- MaxSize)
        // 2) [CPU0]: (speculative) store to _ring[end & MaxSizeMask == 0]
        //                _ring[0] <- element
        // 3) [CPU1]: (pop's) loads from _ring[_begin & MaxSizeMask == 0]
        //                element1 <- _ring[0] (which is trashed at (2))
        // 4) [CPU1]: increments _begin: _begin <- 1
        // 5) [CPU0]: loads _begin: beg <- 1
        // 6) [CPU0]: "if (end (==MaxSize) - beg (==1) >= MaxSize)" is FALSE and
        //            speculative write in (2) is not rolled back.
        //
        // As a result push() will succeed, pop() also succeed but will return a
        // trashed (by push() in step (3)) value.
        //
        unsigned beg = _begin.load(std::memory_order_acquire);

        if (end - beg >= MaxSize) {
            return false;
        }

        _ring[end & MaxSizeMask] = element;
        _end.store(end + 1, std::memory_order_release);

        return true;
    }

    bool pop(T& element)
    {
        unsigned beg = _begin.load(std::memory_order_relaxed);

        if (empty()) {
            return false;
        }

        element = _ring[beg & MaxSizeMask];
        //
        // Use "release" memory order to prevent the reordering of this store
        // and load from the _ring[beg & MaxSizeMask] above.
        //
        // Otherwise there's a possible race when push() already succeeds to
        // trash the element at index "_begin & MaxSizeMask" (when the ring is
        // full) with the new value before the load in this function occurs.
        //
        _begin.store(beg + 1, std::memory_order_release);

        return true;
    }

    /**
     * Checks if the ring is empty(). May be called by both producer and the
     * consumer.
     *
     * @return TRUE if there are no elements
     */
    bool empty() const {
        unsigned beg = _begin.load(std::memory_order_relaxed);
        unsigned end = _end.load(std::memory_order_acquire);
        return beg == end;
    }

    const T& front() const {
        DEBUG_ASSERT(!empty(), "calling front() on an empty queue!");

        unsigned beg = _begin.load(std::memory_order_relaxed);

        return _ring[beg & MaxSizeMask];
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
