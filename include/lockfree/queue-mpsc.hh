/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INCLUDED_LOCKFREE_QUEUE_MPSC
#define INCLUDED_LOCKFREE_QUEUE_MPSC
// A lock-free multiple-producer single-consumer queue implementation.
//
// Multiple-Consumer means that many push()s may be in progress concurrently.
// Single-Consumer means we assume that only a single pop() attempt may be in
// progress - potentially concurrently with push()s.
//
// Note that while there's just one concurrent consumer - it does not have
// to always be the same thread or processor - it's just that the program
// logic guarantees that in no case do two pop()s get called concurrently,
// and the caller is responsible to ensure the appropriate memory ordering.
//
// This is one of the simplest, and most well-known (and often
// reinvented) lock-free algorithm, and we actually have another
// implementation of the same algorithm - with a different API - in
// include/osv/lockless-queue.hh. TODO: merge these two header files.
// Boost also happens to use this algorithm as an example in their
// documentation: see
// http://www.boost.org/doc/libs/1_53_0/doc/html/atomic/usage_examples.html
// #boost_atomic.usage_examples.mp_queue

#include <atomic>

namespace lockfree {

// linked_item<T> is just an example of a type LT which can be passed as a
// parameter to the template queue_mpsc<LT>. But any type LT with a field
// "LT *next" will do as well.
template <class T>
class linked_item {
public:
    T value;
    linked_item<T> *next;
    linked_item<T>() : value(), next(nullptr) { }
    explicit linked_item<T>(T val) : value(val), next(nullptr) { }
 };

// LT can be any type that has an "LT *next" field, which we used to hold a
// pointer to the next item in the queue.
template <class LT>
class queue_mpsc {
private:
    std::atomic<LT*> pushlist;
    LT* poplist;
public:
    constexpr queue_mpsc<LT>() : pushlist(nullptr), poplist(nullptr) { }

    inline void push(LT* item)
    {
        // We set up item to be the new head of the pushlist, pointing to the
        // rest of the existing push list. But while we build this new head
        // node, another concurrent push might have pushed its own items.
        // Therefore we can only replace the head of the pushlist with a CAS,
        // atomically checking that the head is still what we set in
        // item->next, and changing the head.
        // Note that while we have a loop here, it is lock-free - if one
        // competing pusher is paused, the other one can make progress.
        LT *old = pushlist.load(std::memory_order_relaxed);
        do {
            item->next = old;
        } while (!pushlist.compare_exchange_weak(old, item, std::memory_order_release));
    }

    inline LT* pop()
    {
        if (poplist) {
            // The poplist (prepared by an earlier pop) is not empty, so pop
            // from it. We don't need any locking to access the poplist, as
            // it is only touched by pop operations, and our assumption (of a
            // single-consumer queue) is that pops cannot be concurrent.
            LT* result = poplist;
            poplist = poplist->next;
            return result;
        } else {
            // The poplist is empty. Atomically take the entire pushlist
            // (pushers may continue to to push concurrently, so the atomicity
            // is imporant) and then at our leisure, reverse this list (so
            // oldest first) into poplist. We can do this at our leisure
            // because there are no competing pops. Note we need "consume"
            // memory orderinf (because we only access memory via the pointer
            // loaded from the atomic variable), but it appears gcc doesn't
            // support memory_order_consume so we use memory_order_acquire.
            LT *r = pushlist.exchange(nullptr,
                    /*std::memory_order_consume*/std::memory_order_acquire);
            if (!r)
                return nullptr; // the both poplist and poplist were empty
            // Reverse the previous pushlist (now in r) into poplist, and
            // return the last item (the oldest pushed item) as the result
            // of the pop.
            while (r->next) {
                LT *next = r->next;
                r->next = poplist;
                poplist = r;
                r = next;
            }
            return r;
        }
    }

    inline bool empty(void) const
    {
           return (!poplist && !pushlist.load(std::memory_order_relaxed));
    }
};

}
#endif
