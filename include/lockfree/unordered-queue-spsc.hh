/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_LOCKFREE_UNORDERED_QUEUE_SPSC
#define _OSV_LOCKFREE_UNORDERED_QUEUE_SPSC

#include <lockfree/ring.hh>
#include <lockfree/unordered-queue-mpsc.hh>

namespace lockfree {

/**
 * Lock-free malloc-free single-producer single-consumer collection
 * of linkable objects which does not preserve insertion order.
 * It is meant to provide both the speed of a ring buffer and non-blocking
 * properties of linked queues by combining the two.
 *
 * Unlike for ring_spsc, push() is always guaranteed to succeed.
 *
 * LT can be any type that has an "LT *next" field.
 */
template <typename LT, unsigned RingSize>
class unordered_queue_spsc {
private:
    ring_spsc<LT*,RingSize> _ring;
    unordered_queue_mpsc<LT> _queue;
public:

    /**
     * Inserts an object.
     *
     * Preconditions:
     *  - item may not be already queued
     *  - item != nullptr
     *
     * Postconditions:
     *  - item will be returned by one of the subsequent calls to pop()
     */
    inline void push(LT* item)
    {
        if (!_ring.push(item)) {
            _queue.push(item);
        }
    }

   /**
     * Removes some object and returns it or nullptr if there are no objects.
     *
     * Postconditions:
     *  - the class no longer keeps hold of the returned item
     *  - the item may be queued again via push() if != nullptr
     */
    inline LT* pop()
    {
        LT* value;
        if (_ring.pop(value)) {
            return value;
        }

        return _queue.pop();
    }
};

}

#endif
