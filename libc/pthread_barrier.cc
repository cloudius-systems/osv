/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <pthread.h>
#include <osv/debug.hh>
#include <osv/rwlock.h>
#include <osv/latch.hh>
#include "pthread.hh"

// Private definitions of the internal structs backing pthread_barrier_t and
// pthread_barrierattr_t
typedef struct
{
    unsigned int out;
    unsigned int count;
    latch *ltch;
    pthread_mutex_t *mtx;
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
    barrier->out = 0;
    barrier->ltch = new latch(count);
    barrier->mtx = new pthread_mutex_t;
    pthread_mutex_init(barrier->mtx, NULL);
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier_opq)
{
    pthread_barrier_t_int *barrier = (pthread_barrier_t_int*) barrier_opq;
    static_assert(sizeof(pthread_barrier_t_int) <= sizeof(pthread_barrier_t),
                  "pthread_barrier_t_int is larger than pthread_barrier_t");

    if (!barrier || !barrier->ltch || !barrier->mtx) {
        return EINVAL;
    }

    int retval = 0;
    pthread_mutex_t *mtx = barrier->mtx;

    pthread_mutex_lock(mtx);
    pthread_mutex_unlock(mtx);

    latch *l  = barrier->ltch;
    l->count_down();
    // All threads stuck here until we get at least 'count' waiters
    l->await();

    // If the last thread (thread x) to wait on the barrier is descheduled here
    // (immediately after being the count'th thread crossing the barrier)
    // the barrier remains open (a new waiting thread will cross) until
    // the barrier is reset below (when thread x is rescheduled), which doesn't
    // seem technically incorrect. Only one of the crossing threads will get a
    // retval of PTHREAD_BARRIER_SERIAL_THREAD, when
    // barrier->out == barrier->count.
    // All other crossing threads will get a retval of 0.

    pthread_mutex_lock(mtx);
    barrier->out++;
    // Make the last thread out responsible for resetting the barrier's latch.
    // The last thread also gets the special return value
    // PTHREAD_BARRIER_SERIAL_THREAD. Every other thread gets a retval of 0
    if (barrier->out == barrier->count) {
        retval = PTHREAD_BARRIER_SERIAL_THREAD;
        // Reset the latch for the next round of waiters. We're using an
        // external lock (mtx) to ensure that no other thread is calling
        // count_down or in await when we're resetting it. Without the external
        // lock, resetting the latch isn't safe.
        l->unsafe_reset(barrier->count);
        // Reset the 'out' counter so that the equality check above works across
        // multiple rounds of threads waiting on the barrier
        barrier->out = 0;
    }
    pthread_mutex_unlock(mtx);
    return retval;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier_opq)
{
    pthread_barrier_t_int *barrier = (pthread_barrier_t_int*) barrier_opq;

    static_assert(sizeof(pthread_barrier_t_int) <= sizeof(pthread_barrier_t),
                  "pthread_barrier_t_int is larger than pthread_barrier_t");

    if (!barrier || !barrier->ltch || !barrier->mtx) {
        return EINVAL;
    }

    delete barrier->ltch;
    barrier->ltch = nullptr;

    pthread_mutex_destroy(barrier->mtx);
    delete barrier->mtx;
    barrier->mtx = nullptr;

    return 0;
}
