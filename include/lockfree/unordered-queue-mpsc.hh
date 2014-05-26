/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_LOCKFREE_UNORDERED_QUEUE_MPSC
#define _OSV_LOCKFREE_UNORDERED_QUEUE_MPSC

#include <atomic>
#include "arch.hh"

namespace lockfree {

/**
 * Lock-free malloc-free multiple-producer single-consumer collection
 * of linkable objects which does not preserve insertion order.
 *
 * Implementation based on lockfree::queue-mpsc.
 *
 * LT can be any type that has an "LT *next" field.
 */
template <class LT>
class unordered_queue_mpsc {
private:
    std::atomic<LT*> _head CACHELINE_ALIGNED;
    LT* _poll_list CACHELINE_ALIGNED;
public:
    constexpr unordered_queue_mpsc<LT>()
        : _head(nullptr)
        , _poll_list(nullptr)
    {
    }

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
        LT *old = _head.load(std::memory_order_relaxed);
        do {
            item->next = old;
        } while (!_head.compare_exchange_weak(old, item, std::memory_order_release));
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
        if (_poll_list) {
            LT* result = _poll_list;
            _poll_list = _poll_list->next;
            return result;
        } else {
            LT *r = _head.exchange(nullptr, std::memory_order_acquire);
            if (!r) {
                return nullptr;
            }
            _poll_list = r->next;
            return r;
        }
    }
};

}

#endif
