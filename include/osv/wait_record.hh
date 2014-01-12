/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INCLUDED_OSV_WAIT_RECORD
#define INCLUDED_OSV_WAIT_RECORD

#include <sched.hh>

// A "waiter" is simple synchronization object, with which one thread calling
// waiter->wait() goes to sleep, and a second thread, which finds this waiter
// on some wait-queue, wakes it up with waiter->wake().
//
// A waiter->wake() is guaranteed to stop or avoid the wait of waiter->wait(),
// no matter which came before which, or even if the two race. And conversely,
// when wake->wait() returns, this can only happen when waker->wake() was
// called, and cannot happen because of a spurious wakeup.
//
// waiter is a single-use object - each of wake() and wait() can only be
// called once on it.
//
// waiter behaves similarly to the familiar "event semaphore" synchronization
// mechanism (e.g., see Event objects in Python and in Microsoft Windows),
// except that waiter is limited to a single waiting thread.

class waiter {
private:
    sched::thread *t;
public:
    explicit waiter(sched::thread *t) : t(t) { };

    inline void wake() {
        t->wake_with_from_mutex([&] { t = nullptr; });
    }

    inline void wait() const {
        sched::thread::wait_until([&] { return !t; });
    }

    inline void wait(sched::timer* tmr) const {
        sched::thread::wait_until([&] {
            return (tmr && tmr->expired()) || !t; });
    }

    // The thread() method returns the thread waiting on this waiter, or 0 if
    // the waiter was already woken. It shouldn't normally be used except for
    // sanity assert()s. To help enforce this intended use case, we return a
    // const sched::thread.
    inline const sched::thread *thread(void) const {
        return t;
    }

    // woken() returns true if the wait_record was already woken by a wake()
    // (a timeout doesn't set the wait_record to woken()).
    inline bool woken() const {
        return !t;
    }

    // A waiter object cannot be copied or moved, as wake() on the copy will
    // simply zero its copy of the content - not the original content on which
    // wait() is waiting on.
    waiter(const waiter &) = delete;
    waiter &operator=(const waiter &) = delete;
    waiter(waiter &&) = delete;
    waiter &operator=(waiter &&) = delete;

};

// A "wait_record" is simply a "waiter" (see above) with an added "next"
// pointer, which can be used to enqueue a waiter in wait queue.
// Both mutex and condvar use "wait_record" to hold the waiting thread and
// the wake() operation to wake it. The fact we use the same object for both
// means we can easily move a thread waiting on a condvar to wait on a mutex
// (doing that, a technique called "wait morphing", is intended to reduce
// unnecessary thread wakes).

struct wait_record : public waiter {
    struct wait_record *next;
    explicit wait_record(sched::thread *t) : waiter(t), next(nullptr) { };
};

#endif /* INCLUDED_OSV_WAIT_RECORD */
