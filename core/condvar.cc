#include <osv/condvar.h>
#include <sched.h>
#include <errno.h>
#include <osv/wait_record.hh>

int condvar_wait(condvar_t *condvar, mutex_t* user_mutex, uint64_t expiration)
{
    if (expiration) {
        sched::timer timer(*sched::thread::current());
        timer.set(expiration);
        return condvar_wait(condvar, user_mutex, &timer);
    } else {
        return condvar_wait(condvar, user_mutex, nullptr);
    }
}

int condvar_wait(condvar_t *condvar, mutex_t* user_mutex, sched::timer* tmr)
{
    int ret = 0;
    wait_record wr(sched::thread::current());

    mutex_lock(&condvar->m);
    if (!condvar->waiters_fifo.oldest) {
        condvar->waiters_fifo.oldest = &wr;
    } else {
        condvar->waiters_fifo.newest->next = &wr;
    }
    condvar->waiters_fifo.newest = &wr;
    // Remember user_mutex for "wait morphing" feature. Assert our assumption
    // that concurrent waits use the same mutex.
    assert(!condvar->user_mutex || condvar->user_mutex == user_mutex);
    condvar->user_mutex = user_mutex;
    mutex_unlock(user_mutex);
    mutex_unlock(&condvar->m);

    // Wait until either the timer expires or condition variable signaled
    wr.wait(tmr);
    if (!wr.woken()) {
        // wr is still in the linked list (because of a timeout) so remove it:
        mutex_lock(&condvar->m);
        if (&wr == condvar->waiters_fifo.oldest) {
            condvar->waiters_fifo.oldest = wr.next;
            if (!wr.next) {
                condvar->waiters_fifo.newest = nullptr;
            }
        } else {
            wait_record *p = condvar->waiters_fifo.oldest;
            while (p) {
                if (&wr == p->next) {
                    p->next = p->next->next;
                    if(!p->next) {
                        condvar->waiters_fifo.newest = p;
                    }
                    break;
                }
                p = p->next;
            }
        }
        mutex_unlock(&condvar->m);
        ret = ETIMEDOUT;
    }

    mutex_lock(user_mutex);
    return ret;
}

void condvar_wake_one(condvar_t *condvar)
{
    // To make wake with no waiters faster, and avoid unnecessary contention
    // in that case, first check the queue head outside the lock. If it is not
    // empty, we still need to take the lock, and re-read the head.
    if (!condvar->waiters_fifo.oldest) {
        return;
    }

    mutex_lock(&condvar->m);
    wait_record *wr = condvar->waiters_fifo.oldest;
    if (wr) {
        condvar->waiters_fifo.oldest = wr->next;
        if (wr->next == nullptr) {
            condvar->waiters_fifo.newest = nullptr;
        }
        wr->wake();
        // To help the assert() in condvar_wait(), we need to zero saved
        // user_mutex when all concurrent condvar_wait()s are done.
        if (!condvar->waiters_fifo.oldest) {
            condvar->user_mutex = nullptr;
        }
    }
    mutex_unlock(&condvar->m);
}

void condvar_wake_all(condvar_t *condvar)
{
    if (!condvar->waiters_fifo.oldest) {
        return;
    }

    mutex_lock(&condvar->m);
    wait_record *wr = condvar->waiters_fifo.oldest;
    // To help the assert() in condvar_wait(), we need to zero saved
    // user_mutex when all concurrent condvar_wait()s are done.
    condvar->user_mutex = nullptr;
    while (wr) {
        auto next_wr = wr->next; // need to save - *wr invalid after wake
        wr->wake();
        wr = next_wr;
    }
    condvar->waiters_fifo.oldest = condvar->waiters_fifo.newest = nullptr;
    mutex_unlock(&condvar->m);
}
