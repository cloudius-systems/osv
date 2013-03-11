/* A lock-free multiple-producer single-consumer queue implementation.
 * "multiple-consumer" means that many push() may be in progress concurrently,
 * while "single-consumer" means we assume that only a single pop() attempt
 * may be in progress - potentially concurrently with push()s.
 *
 * Note that while there's just one concurrent consumer - it does not have
 * to always be the same thread or processor - it's just that the program
 * logic guarantees that in no case do to pop()s get called concurrently.
 */

#include <atomic>

using namespace std;

namespace lockfree {

// TODO: can we use boost::intrusive::list instead of our own?
template <class T>
class linked_item {
public:
    T value;
    linked_item<T> *next;
    linked_item<T>() : next(nullptr) { }
    linked_item<T>(T val) : value(val), next(nullptr) { }
 };

template <class T>
class queue_mpsc {
public:
    typedef linked_item<T> LT;
private:
    atomic<LT*> pushlist;
    LT* poplist;
    // currently, pop() when the queue is empty returns this "nothing".
    // perhaps we should consider a different error mechanism.
    T nothing;
public:
    queue_mpsc<T>(T nothing) : pushlist(nullptr), poplist(nullptr), nothing(nothing) { }
    inline void push(LT* item)
    {
        // We set up item to be the new head of the pushlist, pointing to the
        // rest of the existing push list. But while we build this new head
        // node, another concurrent push might have pushed its own items.
        // Therefore we can only replace the head of the pushlist with a CAS,
        // atomically checking that the head is still what we set in
        // waiter->next, and changing the head.
        // Note that while we have a loop here, it is lockfree - if one
        // competing pusher is paused, the other one can make progress.
        do {
            item->next = pushlist;
        } while (!std::atomic_compare_exchange_weak(&pushlist, &item->next, item));
    }

    inline T pop(void)
    {
        if (poplist) {
            // The poplist (prepared by an earlier pop) is not empty, so pop
            // from it. We don't need any locking to access the poplist, as
            // it is only touched by pop operations, and our assumption (of a
            // SCMP queue) is that pops cannot be concurrent.
            LT *oldhead = poplist;
            poplist = poplist->next;
            return oldhead->value;
        } else {
            // The poplist is empty. Atomically take the entire pushlist (pushers
            // may continue to to push concurrently, so the atomicity is imporant)
            // and then at our leasure, reverse this list (so oldest first) into
            // poplist. We can do this at our leasure because there are no competing
            // pops.
            LT *r = pushlist.exchange(nullptr);
            // Reverse the previous pushlist (now in r) into poplist, and return
            // the last item (the oldest pushed item) as the result of the pop.
            while(r){
                if(!r->next)
                    return r->value;
                LT *next = r->next;
                r->next = poplist;
                poplist = r;
                r = next;
            }
            // if we're still here, the queue is empty. return "nothing"
            return nothing;
        }
    }

    inline bool isempty(void) const
    {
           return (!poplist && !pushlist.load());
    }
};

}
