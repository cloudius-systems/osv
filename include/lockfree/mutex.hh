#ifndef MUTEX_HH
#define MUTEX_HH

#include <atomic>

#include <sched.hh>
#include <lockfree/queue-mpsc.hh>

// A lock-free mutex implementation, based on the combination of two basic
// techniques:
// 1. Our lock-free multi-producer single-consumer queue technique
//    (see lockfree/queue-mpsc.hh)
// 2. The "responsibility hand-off" protocol described in the 2007 paper
//    "Blocking without Locking or LFTHREADS: A lock-free thread library"
//    by Anders Gidenstam and Marina Papatriantafilou.

class mutex {
private:
    // counts the number of threads holding the lock or waiting for it.
    std::atomic<int> count;
    // "owner" and "depth" are need for implementing a recursive mutex
    unsigned int depth;
    std::atomic<sched::thread *> owner;
    //std::atomic_int active; // NYH try for resolving race condition
    queue_mpsc<sched::thread *> waitqueue;
    // responsiblity hand-off protocol (see comments below)
    std::atomic<sched::thread *> handoff;
    std::atomic<unsigned int> token; // TODO: are 4 bytes enough for the sensible duration of cpu pauses?
public:
    mutex() : count(0), depth(0), owner(nullptr), handoff(nullptr), token(0) { }
    ~mutex() { assert(count==0); }

    void lock()
    {
        sched::thread *current = sched::thread::current();

        if (std::atomic_fetch_add(&count, 1) == 0) {
            // Uncontended case (no other thread is holding the lock, and no
            // concurrent lock() attempts). We got the lock.
            owner.store(current);
            assert(depth==0);
            depth = 1;
            return;
        }

        // If we're here the mutex was already locked, but we're implementing
        // a recursive mutex so it's possible the lock holder is us - in which
        // case we don't need to increment depth instead of waiting.
        if (owner.load() == current) {
            --count; // undo the increment of count (but still leaving it > 0)
            ++depth;
            return;
        }

        // If we're here still here the lock is owned by a different thread,
        // (or we're racing with another lock() which is likely to take the
        // lock). Put this thread in a lock-free waiting queue, so it will
        // eventually be woken when another thread releases the lock.

        // NOTE: the linked-list item "waiter" is allocated on the stack and
        // then used on the wait queue's linked list, but we only exit this
        // function (making waiter invalid) when we know that waiter was
        // removed from the wait list.
        linked_item<sched::thread *> waiter(current);

        // Set current thread to "waiting" state before putting it in the wait
        // queue, so that wake() is allowed on it. After doing prepare_wait()
        // do not return from this function without calling current->wait()!
        current->prepare_wait();
        waitqueue.push(&waiter);

        sched::thread *old_handoff = handoff.load();
        // In the "Responsibility Hand-Off protocol" (G&P, 2007) the unlock()
        // offers a lock() the responsibility of of the mutex (namely, to wake
        // some waiter up), in three steps (listed in increasing time):
        //    1. unlock() offers the lock by setting "handoff" to some
        //       non-null token.
        //    2. a lock() verifies that it has anybody to wake (the queue is
        //       not empty). Often it will be itself.
        //    3. the lock() now atomically verifies that the same handoff is
        //       still in progress as it was during the queue check), and if
        //       it is takes the handoff (makes it null).
        if (old_handoff) {
            // TODO: clean up or remove this explanation
            // "Handoff" protocol (inspired by Gidenstam & Papatriantafilou, 2007):
            // A concurrent unlock() wants to wake up a thread waiting on this
            // mutex, but couldn't find us because it checked the wait queue before
            // we pushed ourselves. The unlocker can't busy-wait for a thread to
            // appear on the wait queue as this can cause it to lock up if a lock()
            // attempt is paused. Instead it flags a "handoff", and one of the
            // waiting threads may pick up the handoff (by atomically zeroing
            // it) and wake somebody (itself or some other waiter that turned up).
            //
            // They key point is that after signaling the handoff, the unlocker
            // does not need to busy-wait - if it knows (from "count") there is
            // a concurrent locker, but sees an empty waitqueue, then it knows
            // the locker is before the waitqueue.push() call above, so it will
            // surely see the handoff flag later.
            if (!waitqueue.isempty()){
                // Typically waitqueue isn't empty because this thread is on
                // it, but it is also possible that some past unlock() already
                // found this thread on the wait list and woke it.
                if (std::atomic_compare_exchange_strong(&handoff, &old_handoff, (sched::thread *)nullptr)) {
                    // We picked up the handoff, so now it is our duty to wake
                    // some waiting thread (could be this thread, or another
                    // thread). At this point (count>0, handoff=null) so the
                    // mutex is considered locked by us so if this cpu pauses now
                    // nothing bad happens except this thread keeping the lock.
                    //
                    // Above, we checked waitqueue was not empty, and
                    // since then nobody else had the opportunity to pop() it
                    // because:
                    // 1. unlock() only calls pop() outside the handoff attempt
                    //    (while handoff=null) but the CAS above verified that
                    //    the handoff attempt did not complete (if it did, the
                    //    token would change).
                    // 2. another lock() only calls pop() after taking the
                    //    handoff, and here we took the handoff (and there
                    //    cannot be another unlock, and another handoff,
                    //    until we wake some lock() attempt)
                    // The fact nobody else pop()ed is important for two
                    // reasons - 1. we know the queue is still not empty,
                    // and 2. the following pop() cannot be concurrent
                    // with another pop() (which our queue implementation
                    // does not support).
                    sched::thread *other_thread = waitqueue.pop();
                    assert(other_thread);
                    assert(depth==0);
                    depth = 1;
                    owner.store(other_thread);
                    other_thread->wake();
                }
            }
        }

        // Wait until another thread wakes us up. When somebody wakes us,
        // they will set us to be the owner first.
        while (true) {
            if (owner.load() == current)
                return;
            current->wait(); // reschedule
            // TODO: can spurious wakes happen? Maybe this loop isn't needed.
        }
        // TODO: do we need to call current->stop_wait() or at least preempt_enabled??
    }

    bool try_lock(){
        sched::thread *current = sched::thread::current();

        int zero = 0;
        if (std::atomic_compare_exchange_strong(&count, &zero, 1)) {
            // Uncontended case (no other thread is holding the lock, and no
            // concurrent lock() attempts). We got the lock.
            owner.store(current);
            assert(depth==0);
            depth = 1;
            return true;
        }

        // If we're here the mutex was already locked, but we're implementing
        // a recursive mutex so it's possible the lock holder is us - in which
        // case we don't need to increment depth instead of waiting.
        if (owner.load() == current) {
            --count; // undo the increment of count (but still leaving it > 0)
            ++depth;
            return true;
        }

        // If we're here still here the lock is owned by a different thread,
        // or we're racing with another lock() which is likely to take the
        // lock. We're almost ready to give up (return false), but the last
        // chance is if we can accept a handoff - and if we do, we got the lock.
        sched::thread *old_handoff = handoff.load();
        if(!old_handoff)
            return false;
        if (std::atomic_compare_exchange_strong(&handoff, &old_handoff, (sched::thread *)nullptr)) {
            // we got the lock!
            ++count;
            owner.store(current);
            assert(depth==0);
            depth = 1;
            return true;
        }

    }
    void unlock(){
        sched::thread *current = sched::thread::current();
        // TODO: decide what to do (exception?) if unlock() is called when we're not the
        // owner.
        // It is fine to check if owner is this thread without race
        // conditions, because no other thread can make it become this thread
        // (or make it stop being this thread)
        if (owner.load() != current) {
            return;
        }
        assert(depth!=0);
        if (depth > 1) {
            // It's fine to --depth without locking or atomicity, as only
            // this thread can possibly change the depth.
            --depth;
            return;
        }


        // If we're here, depth=1 and we need to release the lock.
        owner.store(nullptr);
        depth = 0;
        if (std::atomic_fetch_add(&count, -1) == 1) {
            // No concurrent lock() until we got to count=0, so we can
            // return now. This is the easy case :-)
            return;
        }

        // If we're still here, we noticed that there at least one concurrent
        // lock(). It is possible it put itself on the waitqueue, and if so we
        // need to wake it. It is also possible it didn't yet manage to put
        // itself on the waitqueue in which case we'll tell it or another
        // thread to take the mutex, using the "handoff" protocol.
        // Each iteration of the handoff protocol is an attempt to communicate
        // with an ongoing lock() to get it to take over the mutex. If more
        // than one iteration is needed, we know that at least
        // TODO: it's not completely clear to me why more than two
        // iterations can ever be needed. If the loop continues it means
        // another lock() queued itself - but if it did, wouldn't the next
        // iteration just pop and return?
        while(true) {
            // See discussion in lock() on why we can never have more than
            // one concurrent pop() being called (as our waitqueue
            // implementation assumes). It's important to also note that
            // we can't have two pop() of two unlock() running at the
            // same time (e.g., consider that while this code is stuck
            // here, a lock() and unlock() will succeed on another processor).
            // The reason is that we have a lock() running concurrently
            // and it will not finish until being woken, and we won't do
            // that before doing pop() below
            sched::thread *other_thread = waitqueue.pop();
            if (other_thread) {
                // The thread we'll wake up will expect the mutex's owner to be
                // set to it.
                depth = 1;
                owner.store(other_thread);
                other_thread->wake();
                return;
            }
            // Some concurrent lock() is in progress (we know this because of
            // count) but it hasn't yet put itself on the wait queue.
            sched::thread *ourhandoff = token++;
            handoff = ourhandoff;
            if (waitqueue.isempty()) {
                // If the queue is empty, we know one concurrent lock()
                // is in it code before adding itself to the queue, so it
                // will later see the handoff flag we set here
                // (see comment in lock()).
                return;
            }
            // Something already appeared on the queue, let's try to take
            // the handoff ourselves.
            if (!std::atomic_compare_exchange_strong(&handoff, &ourhandoff, (sched::thread *)nullptr)) {
                // somebody else already took the handoff. That's fine - they will
                // be resonposible for the mutex now.
                return;
            }
        }
    }
};


template <class Lock, class Func>
auto with_lock(Lock& lock, Func func) -> decltype(func())
{
    std::lock_guard<Lock> guard(lock);
    return func();
}

#endif
