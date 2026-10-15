/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/condvar.h>
#include <sched.h>
#include <errno.h>
#include <osv/trace.hh>
#include <osv/wait_record.hh>
#include <osv/export.h>
#include <osv/kernel_config_lazy_stack.h>
#include <osv/kernel_config_lazy_stack_invariant.h>
#include <osv/kernel_config_fork.h>

TRACEPOINT(trace_condvar_wait, "%p", condvar *);
TRACEPOINT(trace_condvar_wake_one, "%p", condvar *);
TRACEPOINT(trace_condvar_wake_all, "%p", condvar *);

int condvar::wait(mutex* user_mutex, sched::timer* tmr)
{
    trace_condvar_wait(this);
    int ret = 0;
#if CONF_fork
    // A fork child (non-AS0) heap-allocates its wait_record so it stays
    // coherent when the parent (different address space) walks this shared
    // condvar's queue.  AS0 keeps the on-stack fast path.
    coherent_wait_record wr_holder(sched::thread::current());
    wait_record &wr = wr_holder.get();
#else
    wait_record wr(sched::thread::current());
#endif

    _m.lock();
    if (!_waiters_fifo.oldest) {
        _waiters_fifo.oldest = &wr;
    } else {
        _waiters_fifo.newest->next = &wr;
    }
    _waiters_fifo.newest = &wr;
    // Remember user_mutex for "wait morphing" feature. Assert our assumption
    // that concurrent waits use the same mutex.
    assert(!_user_mutex || _user_mutex == user_mutex);
    _user_mutex = user_mutex;
    // This preempt_disable() is just an optimization, to avoid context
    // switch between the two unlocks.
#if CONF_lazy_stack_invariant
    assert(arch::irq_enabled());
    assert(sched::preemptable());
#endif
#if CONF_lazy_stack
    arch::ensure_next_stack_page();
#endif
    sched::preempt_disable();
    user_mutex->unlock();
    _m.unlock();
    sched::preempt_enable();

    // Wait until either the timer expires or condition variable signaled
    wr.wait(tmr);
    if (!wr.woken()) {
        ret = ETIMEDOUT;
        // wr is still in the linked list (because of a timeout) so remove it:
        _m.lock();
        if (&wr == _waiters_fifo.oldest) {
            _waiters_fifo.oldest = wr.next;
            if (!wr.next) {
                _waiters_fifo.newest = nullptr;
            }
        } else {
            wait_record *p = _waiters_fifo.oldest;
            while (p) {
                if (&wr == p->next) {
                    p->next = p->next->next;
                    if(!p->next) {
                        _waiters_fifo.newest = p;
                    }
                    break;
                }
                p = p->next;
            }
            if (!p) {
                ret = 0;
            }
        }
        _m.unlock();
        if (!ret) {
            // wr is no longer in the queue, so either wr.wake() is
            // already done, or wake_all() has just taken the whole queue
            // and will wr.wake() soon. We can't return (and invalidate wr)
            // until it calls wr.wake().
            wr.wait();
        }
    }

    if (wr.woken()) {
        // Our wr was woken. The "wait morphing" protocol used by
        // condvar_wake*() ensures that this only happens after we got the
        // user_mutex for ourselves, so no need to mutex_lock() here.
        user_mutex->receive_lock();
    } else {
        user_mutex->lock();
    }

    return ret;
}

void condvar::wake_one()
{
    trace_condvar_wake_one(this);
    // Do NOT early-out on an unlocked read of _waiters_fifo.oldest: the waiter
    // links its wait_record under _m then releases _m, and this reader does not
    // take _m for the pre-check, so there is no acquire pairing that release --
    // the just-linked record can read as a stale nullptr and the wakeup is
    // dropped (see the fuller note in wake_all()).  Take _m first, then check.
    _m.lock();
    wait_record *wr = _waiters_fifo.oldest;
    if (wr) {
        _waiters_fifo.oldest = wr->next;
        if (wr->next == nullptr) {
            _waiters_fifo.newest = nullptr;
        }
        // Rather than wake the waiter here (wr->wake()) and have it wait
        // again for the mutex, we do "wait morphing" - have it continue to
        // sleep until the mutex becomes available.
        _user_mutex->send_lock(wr);
        // To help the assert() in condvar_wait(), we need to zero saved
        // user_mutex when all concurrent condvar_wait()s are done.
        if (!_waiters_fifo.oldest) {
            _user_mutex = nullptr;
        }
    }
    _m.unlock();
}

void condvar::wake_all()
{
    trace_condvar_wake_all(this);
    // Do NOT early-out on an unlocked read of _waiters_fifo.oldest: a waiter
    // links its wait_record under _m and then releases _m, but this reader does
    // not take _m for the check, so there is no acquire to pair with that
    // release and the just-linked record can be read as a stale nullptr --
    // dropping the wakeup.  Under CONF_fork the waiter may be a forked child
    // whose wait_record lives in another address space (coherent_wait_record),
    // widening the window.  A sparse cross-address-space broadcast (e.g. a ZFS
    // zio completion signalling a forked backend blocked in zio_wait) then
    // wakes nobody and the backend hangs forever.  Take _m first, then check.
    _m.lock();
    wait_record *wr = _waiters_fifo.oldest;
    if (!wr) {
        _m.unlock();
        return;
    }

    // To help the assert() in condvar_wait(), we need to zero saved
    // user_mutex when all concurrent condvar_wait()s are done.
    auto user_mutex = _user_mutex;
    _user_mutex = nullptr;

    _waiters_fifo.oldest = _waiters_fifo.newest = nullptr;
    _m.unlock();
    while (wr) {
        auto next_wr = wr->next; // need to save - *wr invalid after wake
        auto cpu_wr = wr->thread()->tcpu();
        user_mutex->send_lock(wr);
        // As an optimization for many threads to wake up on relatively few
        // CPUs, queue all the threads that will likely wake on the same CPU
        // one after another, as same-CPU wakeup is faster.
        wait_record *prevr = nullptr;
        for (auto r = next_wr; r;) {
            auto nextr = r->next;
            if (r->thread()->tcpu() == cpu_wr) {
                user_mutex->send_lock(r);
                if (r == next_wr) {
                    next_wr = nextr;
                } else {
                    prevr->next = nextr;
                }
            } else {
                prevr = r;
            }
            r = nextr;
        }
        wr = next_wr;
    }
}

extern "C" OSV_LIBSOLARIS_API
int condvar_wait(condvar_t *condvar, mutex_t* user_mutex, uint64_t expiration)
{
    if (expiration) {
        return condvar->wait(user_mutex, osv::clock::uptime::time_point(
            std::chrono::nanoseconds(expiration)));
    } else {
        return condvar->wait(user_mutex);
    }
}

OSV_LIBSOLARIS_API
void condvar_wake_one(condvar_t *condvar)
{
    condvar->wake_one();
}

OSV_LIBSOLARIS_API
void condvar_wake_all(condvar_t *condvar)
{
    condvar->wake_all();
}
