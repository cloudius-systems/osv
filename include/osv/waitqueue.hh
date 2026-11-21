/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef WAITQUEUE_HH_
#define WAITQUEUE_HH_

// A waitqueue is similar to a condition variable, but relies on the
// user supplied mutex for internal locking.

#if defined(__cplusplus) && !defined(USE_C_INTERFACE)


#include <sys/cdefs.h>
#include <osv/sched.hh>
#include <osv/wait_record.hh>

/**
 * An efficient synchronization point for threads.
 *
 * A waitqueue is similar to a condition variable, except that
 * it depends on an external mutex for its own synchronization
 * (hence that mutex must be passed to wake_one() and wake_all()
 * in addition to wait().
 *
 * A waitqueue is suitable for use with wait_for(), so you can
 * wait for a timeout or signal, or any other event, concurrently
 * with the waitqueue.
 */
class waitqueue {
private:
    struct {
        // A FIFO queue of waiters - a linked list from oldest (next in line
        // to be woken) towards newest. The wait records themselves are held
        // on the stack of the waiting thread - so no dynamic memory
        // allocation is needed for this list.
        struct wait_record *oldest = {};
        struct wait_record *newest = {};
    } _waiters_fifo;
public:
    /**
     * Wait on the wait queue
     *
     * Wait to be woken (with wake_one() or wake_all()).
     *
     * It is assumed that wait() is called with the given mutex locked.
     * This mutex is unlocked during the wait, and re-locked before wait()
     * returns.
     */
    void wait(mutex& mtx);
    /**
     * Wake one thread waiting on the condition variable
     *
     * Wake one of the threads currently waiting on the condition variable,
     * or do nothing if there is no thread waiting.
     *
     * The thread is not awakened immediately; it will only wake after mtx
     * is released.
     *
     * wake_one() must be called with the mutex held.
     */
    void wake_one(mutex& mtx);
    /**
     * Wake all threads waiting on the condition variable
     *
     * Wake all of the threads currently waiting on the condition variable,
     * or do nothing if there is no thread waiting.
     *
     * The threads are not awakened immediately; they will only wake after mtx
     * is released (one by one).
     *
     * wake_all() must be called with the mutex held.
     */
    void wake_all(mutex& mtx);
    /**
     * Query whether any threads are waiting on the waitqueue.
     *
     * Query whether or not any threads are waiting on the waitqueue.
     * The mutex associated with the waitqueue must be held.
     */
    bool empty() const { return !_waiters_fifo.oldest; }
private:
    void arm(mutex& mtx);
    bool poll() const;
    class waiter;
    friend class sched::wait_object<waitqueue>;
};

namespace sched {

template <>
class wait_object<waitqueue> {
public:
#if CONF_fork
    // The wait_record links this waiter onto the waitqueue's _waiters_fifo,
    // which a waker in a DIFFERENT (forked-child) address space walks and
    // dereferences.  A stack-resident record sits at an app-slot VA that is
    // private (COW) per address space -- every forked backend's stack is a
    // same-VA private copy -- so a cross-AS waker would read the wrong physical
    // page (a lost/misdirected wakeup: e.g. the RX/poll thread failing to wake
    // an epoll-waiting PG backend, hanging concurrent connections).  Route the
    // record through coherent_wait_record so it lands on the identity kernel
    // heap whenever a cross-AS wake is possible, exactly like lockfree::mutex
    // and condvar; AS0 with no live children keeps the on-stack fast path.
    wait_object(waitqueue& wq, mutex* mtx)
        : _wq(wq), _mtx(*mtx), _holder(sched::thread::current()) {}
    bool poll() const { return _holder.get().woken(); }
    void arm();
    void disarm();
private:
    waitqueue& _wq;
    mutex& _mtx;
    coherent_wait_record _holder;
    wait_record &record() { return _holder.get(); }
#else
    wait_object(waitqueue& wq, mutex* mtx)
        : _wq(wq), _mtx(*mtx), _wr(sched::thread::current()) {}
    bool poll() const { return _wr.woken(); }
    void arm();
    void disarm();
private:
    waitqueue& _wq;
    mutex& _mtx;
    wait_record _wr;
    wait_record &record() { return _wr; }
#endif
};

}

#else

struct waitqueue {
    struct wait_record *oldest;
    struct wait_record *newest;
};

typedef struct waitqueue waitqueue;

#endif

#endif /* WAITQUEUE_HH_ */
