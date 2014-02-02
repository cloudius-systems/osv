/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <mutex>
#include <set>
#include "drivers/clock.hh"
#include "osv/trace.hh"
#include <osv/debug.hh>
#include <osv/sched.hh>

#include <bsd/porting/rwlock.h>
#include <bsd/porting/callout.h>
#include <bsd/porting/netport.h>
#include <bsd/porting/sync_stub.h>

TRACEPOINT(trace_callout_init, "C=%p", void *);
TRACEPOINT(trace_callout_reset, "C=%p to_ticks=%d fn=%p arg=%p", void *, uint64_t, void *, void *);
TRACEPOINT(trace_callout_stop_wait, "C=%p", void *);
TRACEPOINT(trace_callout_stop, "C=%p flags=%d, is_drain=%d", void *, int, int);
TRACEPOINT(trace_callout_thread_waiting, "C=%p", void *);
TRACEPOINT(trace_callout_thread_retry, "C=%p", void *);
TRACEPOINT(trace_callout_thread_dispatching, "C=%p fn=%p", void *, void *);
TRACEPOINT(trace_callout_thread_waking, "C=%p thread=%p", void *, void *);

int _callout_stop_safe_locked(struct callout *c, int safe);

namespace callouts {

    struct callout_compare {
        bool operator() (callout *a, callout *b) const {
            if (a->c_to_ns == b->c_to_ns) {
                return (a < b);
            }

            return (a->c_to_ns < b->c_to_ns);
        }
    };

    // We manage callouts in a single ordered set
    std::set<callout *, callout_compare> _callouts;

    // Both global data and callout structure are protected using this lock
    mutex _callout_mutex;
    void lock(void) { _callout_mutex.lock(); }
    void unlock(void) { _callout_mutex.unlock(); }

    // The callout dispatcher thread
    sched::thread *_callout_dispatcher = nullptr;
    bool _have_work = false;

    void add_callout(callout *c)
    {
        _callouts.insert(c);
    }

    void remove_callout(callout *c)
    {
        _callouts.erase(c);
    }

    callout *get_one(void)
    {
        if (_callouts.empty()) {
            return (nullptr);
        }
        return (*_callouts.begin());
    }

    // FIXME: optimize this function
    bool have_callout(callout *c)
    {
        for (auto i: _callouts) {
            if (i == c) {
                return (true);
            }
        }

        return (false);
    }

    void mark_have_work(void)
    {
        _have_work = true;
    }

    // wakes the dispatcher
    void wake_dispatcher(void)
    {
        _callout_dispatcher->wake();
    }
}

static sched::thread* callout_get_waiter(struct callout *c)
{
    return reinterpret_cast<sched::thread*>(c->waiter_thread);
}

static void callout_set_waiter(struct callout *c, sched::thread* t)
{
    if (t != NULL)
        assert(c->waiter_thread == NULL);

    c->waiter_thread = reinterpret_cast<void*>(t);
}

static void _callout_thread(void)
{
    callouts::lock();

    while (true) {

        // Wait for work
        sched::thread::wait_until(callouts::_callout_mutex, [] {
            return (callouts::get_one() != nullptr);
        });

        // get the first callout with the earliest time
        callout *c = callouts::get_one();

        assert(c->c_flags & (CALLOUT_ACTIVE | CALLOUT_PENDING));

        //////////////////////
        // Wait for timeout //
        //////////////////////

        uint64_t cur = clock::get()->time();
        bool expired = true;
        if (cur < c->c_to_ns-TMILISECOND) {
            sched::timer t(*sched::thread::current());
            t.set(c->c_to_ns);

            trace_callout_thread_waiting(c);
            sched::thread::wait_until(callouts::_callout_mutex, [&] {
                return ( (t.expired()) || (callouts::_have_work));
            });

            callouts::_have_work = false;
            expired = t.expired();
        }

        if (!expired  || (!callouts::have_callout(c))) {
            trace_callout_thread_retry(c);
            continue;
        }

        ///////////////
        // Dispatch! //
        ///////////////

        auto fn = c->c_fn;
        auto arg = c->c_arg;
        struct mtx* c_mtx = c->c_mtx;
        struct rwlock* c_rwlock = c->c_rwlock;
        bool return_unlocked = ((c->c_flags & CALLOUT_RETURNUNLOCKED) == 0);

        if (c_rwlock)
            rw_wlock(c_rwlock);
        if (c_mtx)
            mtx_lock(c_mtx);

        c->c_flags &= ~CALLOUT_PENDING;

        callouts::unlock();

        // Callout handler
        trace_callout_thread_dispatching(c, (void*)fn);
        fn(arg);

        callouts::lock();

        sched::thread* waiter = nullptr;

        //
        // note: after the handler have been invoked the callout structure
        // can look much differently, the handler may reschedule the callout
        // or even freed it.
        //
        // if the callout is in the set it means that it hasn't been freed
        // by the user
        //
        // reset || drain || !stop
        if (callouts::have_callout(c)) {

            waiter = callout_get_waiter(c);
            callout_set_waiter(c, NULL);
            // if the callout hadn't been reschedule, remove it
            if ( ((c->c_flags & CALLOUT_PENDING) == 0) || (waiter) ) {
                c->c_flags |= CALLOUT_COMPLETED;
                callouts::remove_callout(c);
            }
        }

        // FIXME: should we do this in case the caller called callout_stop?
        if (return_unlocked) {
            if (c_rwlock)
                rw_wunlock(c_rwlock);
            if (c_mtx)
                mtx_unlock(c_mtx);
        }

        // if we have a waiter then the callout structure must be valid
        if (waiter) {
            trace_callout_thread_waking(c, waiter);
            waiter->wake();
        }
    }
}

int callout_reset_on(struct callout *c, u64 to_ticks, void (*fn)(void *),
    void *arg, int ignore_cpu)
{
    u64 cur = clock::get()->time();
    int cur_ticks = ns2ticks(cur);
    int result = 0;
    bool queued_first = false;

    callouts::lock();

    trace_callout_reset(c, to_ticks, (void*)fn, arg);

    result = _callout_stop_safe_locked(c, 0);

    // Reset the callout
    c->c_ticks = to_ticks;
    c->c_time = cur_ticks + to_ticks;           // for freebsd compatibility
    c->c_to_ns = cur + ticks2ns(to_ticks);      // this is what we use
    c->c_fn = fn;
    c->c_arg = arg;
    c->c_flags |= (CALLOUT_PENDING | CALLOUT_ACTIVE);

    callouts::add_callout(c);
    if (c == callouts::get_one()) {
        callouts::mark_have_work();
        queued_first = true;
    }

    callouts::unlock();

    if (queued_first)
        callouts::wake_dispatcher();

    return result;
}

// callout_stop() and callout_drain()
int _callout_stop_safe_locked(struct callout *c, int is_drain)
{
    int result = 0;

    trace_callout_stop(c, c->c_flags, is_drain);

    if ((is_drain) &&
        (sched::thread::current() != callouts::_callout_dispatcher) &&
            (callout_pending(c) ||
             (callout_active(c) && !callout_completed(c))) ) {

        // Wait for callout
        callout_set_waiter(c, sched::thread::current());
        callouts::mark_have_work();
        callouts::wake_dispatcher();

        trace_callout_stop_wait(c);

        sched::thread::wait_until(callouts::_callout_mutex, [&] {
            return (c->c_flags & CALLOUT_COMPLETED);
        });

        result = 1;
    }

    callouts::remove_callout(c);

    // Clear flags
    c->c_flags &= ~(CALLOUT_ACTIVE | CALLOUT_PENDING | CALLOUT_COMPLETED);

    return (result);
}

int _callout_stop_safe(struct callout *c, int is_drain)
{
    int result = 0;

    callouts::lock();
    result = _callout_stop_safe_locked(c, is_drain);
    callouts::unlock();

    return (result);
}

void callout_init(struct callout *c, int mpsafe)
{
    bzero(c, sizeof *c);
    assert(mpsafe != 0);

    trace_callout_init(c);
}

void callout_init_rw(struct callout *c, struct rwlock *rw, int flags)
{
    assert(rw != NULL);

    callout_init(c, 1);
    c->c_rwlock = rw;
    c->c_flags = flags;
}

void callout_init_mtx(struct callout *c, struct mtx *mtx, int flags)
{
    assert(mtx != NULL);

    callout_init(c, 1);
    c->c_mtx = mtx;
    c->c_flags = flags;
}

void init_callouts(void)
{
    // Start the callout thread
    callouts::_callout_dispatcher = new sched::thread(_callout_thread,
            sched::thread::attr().name("callout"));
    callouts::_callout_dispatcher->start();
}

