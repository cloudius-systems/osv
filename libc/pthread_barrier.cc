/*
 * Copyright (C) 2017 ScyllaDB, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <pthread.h>
#include <osv/mutex.h>
#include <osv/waitqueue.hh>

// Private definitions of the internal structs backing pthread_barrier_t and
// pthread_barrierattr_t
typedef struct
{
    // "count" is barrier's count set by pthread_barrier_init()
    unsigned int count;
    // "state" uses positive integers to count the number of threads which
    // are waiting on the barrier, and then negative integers to count the
    // number of threads that have been released from the wait; The
    // second part is necessary for correct reusability of the barrier
    // (so the threads can wait on the same barrier again).
    // "state" starts at 0 and goes from 1 through count-1 as more threads
    // wait on the barrier. When the count'th thread reaches the barrier,
    // "state" is negated to -count + 1, all waiting threads are woken, as
    // as each is woken it increases the negative "state" further until
    // finally all waiting threads have woken, state reaches 0 again, and
    // the whole thing can start again.
    int state;
    // "mtx" is the mutex used to protect "state" as well as the condition
    // variable "cv". We actually used a waitqueue rather than a condvar -
    // the main difference is that a waitqueue is protected by our mutex
    // mtx, rather than having a second internal mutex.
    mutex* mtx;
    waitqueue* cv;
} pthread_barrier_t_int;

typedef struct
{
    unsigned pshared;
} pthread_barrierattr_t_int;

int pthread_barrier_init(pthread_barrier_t *barrier_opq,
                         const pthread_barrierattr_t *attr_opq,
                         unsigned count)
{
    pthread_barrier_t_int *barrier = (pthread_barrier_t_int*) barrier_opq;
    static_assert(sizeof(pthread_barrier_t_int) <= sizeof(pthread_barrier_t),
                  "pthread_barrier_t_int is larger than pthread_barrier_t");

    // Linux returns EINVAL if count == 0 or INT_MAX so we do too.
    // In theory, we could go up to UINT_MAX since count is unsigned.
    if (!barrier || count == 0 || count >= INT_MAX) {
        return EINVAL;
    }

    // Always ignore attr, it has no meaning in the context of a unikernel.
    // pthread_barrierattr_t has a single member variable pshared that can be set
    // to PTHREAD_PROCESS_PRIVATE or PTHREAD_PROCESS_SHARED. These have the
    // same effect in a unikernel - there is only a single process and all
    // threads can manipulate the memory area associated with the
    // pthread_barrier_t so it doesn't matter what the value of pshared is set to
    barrier->count = count;
    barrier->state = 0;
    barrier->mtx = new mutex;
    barrier->cv = new waitqueue;
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier_opq)
{
    pthread_barrier_t_int *barrier = (pthread_barrier_t_int*) barrier_opq;
    static_assert(sizeof(pthread_barrier_t_int) <= sizeof(pthread_barrier_t),
                  "pthread_barrier_t_int is larger than pthread_barrier_t");

    if (!barrier || !barrier->count || !barrier->mtx) {
        return EINVAL;
    }

    SCOPE_LOCK(*barrier->mtx);
    while (barrier->state < 0) {
        // Can't start another round until all threads exited the previous
        // round.
        barrier->cv->wait(*barrier->mtx);
    }
    if (++barrier->state == (int)barrier->count) {
        // We're the last of the count threads to enter the barrier.
        barrier->state = -barrier->count + 1;
        barrier->cv->wake_all(*barrier->mtx);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    } else {
        // Not enough threads have reached the barrier yet. Wait until
        // the count'th one enters, and changes barrier->state to negative.
        while (barrier->state > 0) {
            barrier->cv->wait(*barrier->mtx);
        }
        // We're in negative state, and increasing it for every thread
        // which is done waiting in this round. If the state reaches
        // 0 again, everybody who's already waiting for the next round
        // can be woekn.
        if (++barrier->state == 0) {
            barrier->cv->wake_all(*barrier->mtx);
        }
        return 0;
    }
}

int pthread_barrier_destroy(pthread_barrier_t *barrier_opq)
{
    pthread_barrier_t_int *barrier = (pthread_barrier_t_int*) barrier_opq;

    static_assert(sizeof(pthread_barrier_t_int) <= sizeof(pthread_barrier_t),
                  "pthread_barrier_t_int is larger than pthread_barrier_t");

    if (!barrier || !barrier->count || !barrier->mtx) {
        return EINVAL;
    }

    // One of threads, probably that which got PTHREAD_BARRIER_SERIAL_THREAD,
    // might decide to delete the barrier while the other threads have not
    // yet returned from their barrier_wait. So we may need to wait for
    // those threads to complete before destroying the barrier:
    WITH_LOCK(*barrier->mtx) {
        while (barrier->state < 0) {
            barrier->cv->wait(*barrier->mtx);
        }
    }

    delete barrier->mtx;
    delete barrier->cv;
    barrier->mtx = nullptr;
    return 0;
}
