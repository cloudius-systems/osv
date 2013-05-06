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
// logic guarantees that in no case do two pop()s get called concurrently.
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

template <class T>
class linked_item {
public:
    T value;
    linked_item<T> *next;
    linked_item<T>() : value(), next(nullptr) { }
    explicit linked_item<T>(T val) : value(val), next(nullptr) { }
 };

template <class T>
class queue_mpsc {
public:
    typedef linked_item<T> LT;
private:
    std::atomic<LT*> pushlist;
    // While poplist isn't used concurrently, we still want to allow it to
    // be accessed from different threads, and when we read the pointer
    // from poplist in one CPU we need the list it points to to also be
    // visible (so-called "release/consume" memory ordering).
    std::atomic<LT*> poplist;
public:
    queue_mpsc<T>() : pushlist(nullptr), poplist(nullptr) { }

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

    inline bool pop(T *result)
    {
        LT *head = poplist.load(std::memory_order_consume);
        if (head) {
            // The poplist (prepared by an earlier pop) is not empty, so pop
            // from it. We don't need any locking to access the poplist, as
            // it is only touched by pop operations, and our assumption (of a
            // single-consumer queue) is that pops cannot be concurrent.
            *result = head->value;
            poplist.store(head->next, std::memory_order_release);
            return true;
        } else {
            // The poplist is empty. Atomically take the entire pushlist (pushers
            // may continue to to push concurrently, so the atomicity is imporant)
            // and then at our leasure, reverse this list (so oldest first) into
            // poplist. We can do this at our leasure because there are no competing
            // pops. Note we need memory_order_consume (because we only access memory
            // via the pointer loaded from the atomic variable), but it appears gcc
            // doesn't support memory_order_consume so we use memory_order_acquire.
            LT *r = pushlist.exchange(nullptr, /*std::memory_order_consume*/std::memory_order_acquire);
            if (!r)
                return false; // the both poplist and poplist were empty
            // Reverse the previous pushlist (now in r) into poplist, and return
            // the last item (the oldest pushed item) as the result of the pop.
            while (r->next) {
                LT *next = r->next;
                r->next = head;
                head = r;
                r = next;
            }
            *result = r->value;
            poplist.store(head, std::memory_order_release);
            return true;
        }
    }

    inline bool empty(void) const
    {
           return (!poplist.load(std::memory_order_relaxed) &&
                   !pushlist.load(std::memory_order_relaxed));
    }
};

}
#endif
