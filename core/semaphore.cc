/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/semaphore.hh>
#include <osv/kernel_config_fork.h>
#if CONF_fork
#include <osv/wait_record.hh>
#endif

semaphore::semaphore(unsigned val)
    : _val(val)
{
}

void semaphore::post_unlocked(unsigned units)
{
    _val += units;
    auto i = _waiters.begin();
    while (_val > 0 && i != _waiters.end()) {
        auto wr = i++;
        if (wr->units <= _val) {
            _val -= wr->units;
            wr->owner->wake();
            wr->owner = nullptr;
            _waiters.erase(wr);
        }
    }
}

bool semaphore::wait(unsigned units, sched::timer* tmr)
{
#if CONF_fork
    // A shared (pshared) semaphore is dereferenced from MULTIPLE forked address
    // spaces: a poster (sem_post) in one backend walks _waiters and reads each
    // waiter's wait_record -- its owner thread and unit count -- to wake it.
    // If that record lived on the WAITER's stack it would sit at an app-slot VA
    // that is private (COW) to the waiter's address space; every backend's main
    // stack is a per-fork private copy at the SAME VA, so a poster in another
    // backend reading _waiters would dereference ITS OWN stack page (wrong data)
    // or an unmapped page -> a page fault in sem_post's non-preemptable
    // _mtx-locked section (Assertion sched::preemptable()) or a lost wakeup.
    // When a cross-AS wake is possible put the record on the identity kernel
    // heap instead: reachable and coherent from every address space.  This is
    // the SAME rule and gate lockfree::mutex uses for its stack wait_record
    // (see core/lfmutex.cc, include/osv/wait_record.hh).  AS0 with no live
    // fork children keeps the zero-overhead on-stack fast path.
    bool heap_wr = fork_child_needs_heap_wait_record();
    alignas(wait_record) char wr_stack[sizeof(wait_record)];
    wait_record *wrp;
    if (heap_wr) {
        fork_arena::kernel_heap_scope _fkh;
        wrp = new wait_record;
    } else {
        wrp = new (wr_stack) wait_record;
    }
    wait_record &wr = *wrp;
#else
    wait_record wr;
#endif
    wr.owner = nullptr;

    bool acquired;
    {
        std::lock_guard<mutex> guard(_mtx);

        if (_val >= units) {
            _val -= units;
            acquired = true;
        } else {
            wr.owner = sched::thread::current();
            wr.units = units;
            _waiters.push_back(wr);

            sched::thread::wait_until(_mtx,
                    [&] { return (tmr && tmr->expired()) || !wr.owner; });

            // if wr.owner, it's a timeout - post() didn't wake us and didn't
            // decrease the semaphore's value for us. Note we are holding the
            // mutex, so there can be no race with post(). To clean up we should
            // remove the wait record we just pushed onto _waiters.
            if (wr.owner) {
               _waiters.erase(_waiters.iterator_to(wr));
            }
            acquired = !wr.owner;
        }
    }
#if CONF_fork
    if (heap_wr) {
        fork_arena::kernel_heap_scope _fkh; delete wrp;
    } else {
        wrp->~wait_record();
    }
#endif
    return acquired;
 }

bool semaphore::trywait(unsigned units)
{
    bool ok = false;
    WITH_LOCK(_mtx) {
        if (_val >= units) {
            _val -= units;
            ok = true;
        }
    }

    return ok;
}




