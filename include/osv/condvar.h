/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// condvar is OSv's implementation of the classic "condition variable"
// synchronization primitive. It is similar to condition variables in POSIX
// Threads, but makes one additional guarantees that POSIX Threads do not:
// Our condvar_wait() is guaranteed not to have "spurious wakeups", i.e.,
// condvar_wait() will only return if someone called condvar_wake_one()/all().
//
// The difference is subtle. Usually you still need to check the condition
// after condvar_wait() returns because other threads may race us to change
// the condition. But in some cases we use this guarantee. One example is our
// semaphore implementation (semaphore.cc): In the traditional implementation
// when spurious condvar_wait wakeups are possible, the wakee decrements the
// semaphore's counter (this can cause a "thundering herd" problem). Using
// our condvar without spurious wakeups, it is possible to decrement the
// counter in the waker, and decide exactly who to wake.
//
// So watch out - if this implementation is ever rewritten, it should continue
// to guarantee no-spurious-wakeups - even if POSIX Threads doesn't need it.

#ifndef CONDVAR_H_
#define CONDVAR_H_

#include <stdint.h>

#include <osv/mutex.h>

#ifdef __cplusplus

// While the condvar type can be used in C, to C++ code we offer additional
// convenience methods and functions, for which we need these headers:
#include "sched.hh"

#endif

// Note: To be useful for implementing pthread's condition variables, the
// condvar_t structure doesn't need any special initialization beyond zero
// initialization (note PTHREAD_COND_INITIALIZER is all zeros). For this
// to work, mutex_t which we use below, should also be ok with zero
// initialization - and in the current implementation it indeed is.
//
// Moreover, we also don't have a de-initialization function, as there
// is no memory dynamically allocated, nor is there anything meaningful
// to do when the waiter list is not empty.

struct wait_record;

/**
 * Condition Variable
 *
 * condvar implements the classic "condition variable" synchronization
 * primitive, which together with a mutex is used to allow threads to wait
 * until a certain condition occurs.
 *
 * condvar is similar to condition variables in POSIX Threads, but makes one
 * additional guarantees that POSIX Threads do not: Our wait() is
 * guaranteed not to have "spurious wakeups", i.e., condvar_wait() will only
 * return if someone called condvar_wake_one()/all().
 *
 * This difference is subtle. Usually you still need to check the condition
 * after condvar_wait() returns because other threads may race us to change
 * the condition. But in some cases we use this guarantee. One example is our
 * semaphore implementation (semaphore.cc): In the traditional implementation
 * when spurious wait() wakeups are possible, the wakee decrements the
 * semaphore's counter() this can cause a "thundering herd" problem). Using
 * our condvar without spurious wakeups, it is possible to decrement the
 * counter in the waker, and decide exactly who to wake.
 */
typedef struct condvar {
    mutex_t _m;
    struct {
        // A FIFO queue of waiters - a linked list from oldest (next in line
        // to be woken) towards newest. The wait records themselves are held
        // on the stack of the waiting thread - so no dynamic memory
        // allocation is needed for this list.
        struct wait_record *oldest;
        struct wait_record *newest;
    } _waiters_fifo;
    // Remember mutex last used in a wait(), for use in "wait morphing"
    // feature. We disallow (as Posix Threads do) using different mutexes in
    // concurrent wait()s on the same condvar. We could lift this requirement,
    // but then we would need to remember the user_mutex on each wait_record.
    mutex_t *_user_mutex;

#ifdef __cplusplus
    // In C++, for convenience also provide methods.
    condvar() { memset(this, 0, sizeof *this); }
    /**
     * Wait on the condition variable, or timer to expire
     *
     * Wait to be woken (with wake_one() or wake_all()), or the given timer
     * to expire, whichever occurs first. If the timer is nullptr, or omitted,
     * wait indefinitely until woken.
     *
     * It is assumed that wait() is called with the given mutex locked.
     * This mutex is unlocked during the wait, and re-locked before wait()
     * returns.
     *
     * The current implementation assumes (as do Posix Threads) that when
     * multiple threads wait on the same condition variable concurrently,
     * they all do it with the same mutex. If two threads concurrently use
     * two different mutexes to wait on the same condition variable, the
     * results are undefined (currently, it causes an assertion failure).
     *
     * \return 0 if woken by a wake_one() or wake_all(), or ETIMEOUT (!=0)
     * if was not woken, but the timer expired. Note if a wakeup and the
     * timeout race, by the time wait() returns the timer may have already
     * expired even if 0 is returned. What a return of 0 means is not that
     * a timeout hasn't yet occurred, but rather that one wake_one()/wake_all()
     * was consumed to wake us.
     */
    inline int wait(mutex_t *user_mutex, sched::timer *tmr = nullptr);
    /**
     * \overload
     */
    inline int wait(mutex_t &user_mutex, sched::timer *tmr = nullptr);
    /**
     * Wake one thread waiting on the condition variable
     *
     * Wake one of the threads currently waiting on the condition variable,
     * or do nothing if there is no thread waiting.
     *
     * wake_one() may be called with the mutex either locked, or unlocked.
     * If the mutex is locked, the target thread will not be actually woken
     * until the waking thread unlocks it and the target thread can have it.
     * This optimization is known as "wait morphing".
     */
    inline void wake_one();
    /**
     * Wake all threads waiting on the condition variable
     *
     * Wake all of the threads currently waiting on the condition variable,
     * or do nothing if there is no thread waiting.
     *
     * If more than one thread is waiting, they will not all be woken
     * concurrently as all will need the same mutex and most will need to
     * go back to sleep (this is known as the "thundering herd problem").
     * Rather, only one thread is woken, and the rest are moved to the
     * waiting list of the mutex, to be woken up one by one as the mutex
     * becomes available. This optimization is known as "wait morphing".
     */
    inline void wake_all();
    template <class Pred>
    void wait_until(mutex& mtx, Pred pred);
#endif
} condvar_t;

#define CONDVAR_INITIALIZER	{}


#ifdef __cplusplus
extern "C" {
#endif

int condvar_wait(condvar_t *condvar, mutex_t* user_mutex, uint64_t expiration);
void condvar_wake_one(condvar_t *condvar);
void condvar_wake_all(condvar_t *condvar);

#ifdef __cplusplus
}

// additional convenience functions for C++
int condvar_wait(condvar_t *condvar, mutex_t *user_mutex, sched::timer *tmr);
int condvar_t::wait(mutex_t *user_mutex, sched::timer *tmr) {
    return condvar_wait(this, user_mutex, tmr);
}
int condvar_t::wait(mutex_t &user_mutex, sched::timer *tmr) {
    return condvar_wait(this, &user_mutex, tmr);
}
void condvar_t::wake_one() {
    return condvar_wake_one(this);
}
void condvar_t::wake_all() {
    return condvar_wake_all(this);
}

template <class Pred>
inline
void condvar::wait_until(mutex& mtx, Pred pred)
{
    while (!pred()) {
        wait(&mtx);
    }
}


#endif

#endif /* MUTEX_H_ */
