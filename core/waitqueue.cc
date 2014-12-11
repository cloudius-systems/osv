/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/waitqueue.hh>
#include <osv/trace.hh>
#include <osv/wait_record.hh>

TRACEPOINT(trace_waitqueue_wait, "%p", waitqueue *);
TRACEPOINT(trace_waitqueue_wake_one, "%p", waitqueue *);
TRACEPOINT(trace_waitqueue_wake_all, "%p", waitqueue *);

namespace sched {

void wait_object<waitqueue>::arm()
{
    auto& fifo = _wq._waiters_fifo;
    if (!fifo.oldest) {
        fifo.oldest = &_wr;
    } else {
        fifo.newest->next = &_wr;
    }
    fifo.newest = &_wr;
}

void wait_object<waitqueue>::disarm()
{
    auto& fifo = _wq._waiters_fifo;
    if (_wr.woken()) {
        return;
    }
    // wr is still in the linked list, so remove it:
    wait_record** pnext = &fifo.oldest;
    wait_record* newest = nullptr;
    while (*pnext) {
        if (&_wr == *pnext) {
            *pnext = _wr.next;
            if (!*pnext || !(*pnext)->next) {
                fifo.newest = newest;
            }
            break;
        }
        newest = *pnext;
        pnext = &(*pnext)->next;
    }
}

}

void waitqueue::wait(mutex& mtx)
{
    trace_waitqueue_wait(this);
    sched::thread::wait_for(mtx, *this);
}

void waitqueue::wake_one(mutex& mtx)
{
    trace_waitqueue_wake_one(this);
    wait_record *wr = _waiters_fifo.oldest;
    if (wr) {
        _waiters_fifo.oldest = wr->next;
        if (wr->next == nullptr) {
            _waiters_fifo.newest = nullptr;
        }
        // Rather than wake the waiter here (wr->wake()) and have it wait
        // again for the mutex, we do "wait morphing" - have it continue to
        // sleep until the mutex becomes available.
        wr->wake_lock(&mtx);
    }
}

void waitqueue::wake_all(mutex& mtx)
{
    trace_waitqueue_wake_all(this);
    wait_record *wr = _waiters_fifo.oldest;
    _waiters_fifo.oldest = _waiters_fifo.newest = nullptr;
    while (wr) {
        auto next_wr = wr->next; // need to save - *wr invalid after wake
        // FIXME: splice the entire chain at once?
        wr->wake_lock(&mtx);
        wr = next_wr;
    }
}

