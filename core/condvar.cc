#include <osv/condvar.h>
#include <sched.h>
#include <errno.h>

struct ccondvar_waiter {
    struct ccondvar_waiter *newer;
    sched::thread *t;
};

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
    struct ccondvar_waiter wr = { nullptr, sched::thread::current() };

    mutex_lock(&condvar->m);
    if (!condvar->waiters_fifo.oldest) {
        condvar->waiters_fifo.oldest = &wr;
    } else {
        condvar->waiters_fifo.newest->newer = &wr;
    }
    condvar->waiters_fifo.newest = &wr;
    mutex_unlock(&condvar->m);

    mutex_unlock(user_mutex);
    // Wait until either the timer expires or condition variable signaled
    sched::thread::wait_until([&] {
        return (tmr && tmr->expired()) || !wr.t;
    });
    if (wr.t) {
        // wr is still in the linked list (because of a timeout) so remove it:
        mutex_lock(&condvar->m);
        if (&wr == condvar->waiters_fifo.oldest) {
            condvar->waiters_fifo.oldest = wr.newer;
            if (!wr.newer) {
                condvar->waiters_fifo.newest = nullptr;
            }
        } else {
            struct ccondvar_waiter *p = condvar->waiters_fifo.oldest;
            while (p) {
                if (&wr == p->newer) {
                    p->newer = p->newer->newer;
                    if(!p->newer) {
                        condvar->waiters_fifo.newest = p;
                    }
                    break;
                }
                p = p->newer;
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
    mutex_lock(&condvar->m);
    struct ccondvar_waiter *wr = condvar->waiters_fifo.oldest;
    if (wr) {
        condvar->waiters_fifo.oldest = wr->newer;
        if (wr->newer == nullptr) {
            condvar->waiters_fifo.newest = nullptr;
        }
        wr->t->wake_with([&] { wr->t = nullptr; });
    }
    mutex_unlock(&condvar->m);
}

void condvar_wake_all(condvar_t *condvar)
{
    mutex_lock(&condvar->m);
    struct ccondvar_waiter *wr = condvar->waiters_fifo.oldest;
    while (wr) {
        auto next_wr = wr->newer; // need to save - *wr invalid after wake
        wr->t->wake_with([&] { wr->t = nullptr; });
        wr = next_wr;
    }
    condvar->waiters_fifo.oldest = condvar->waiters_fifo.newest = nullptr;
    mutex_unlock(&condvar->m);
}
