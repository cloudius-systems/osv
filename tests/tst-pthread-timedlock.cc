/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises pthread_mutex_timedlock / pthread_mutex_clocklock.
// Built and run on the OSv test image.

#include <pthread.h>
#include <time.h>
#include <errno.h>

#include <cassert>
#include <iostream>

static struct timespec deadline_in(clockid_t clk, long ms)
{
    struct timespec ts;
    clock_gettime(clk, &ts);
    ts.tv_nsec += (ms % 1000) * 1000000L;
    ts.tv_sec += ms / 1000 + ts.tv_nsec / 1000000000L;
    ts.tv_nsec %= 1000000000L;
    return ts;
}

struct tl_arg { pthread_mutex_t *m; int result; };

static void *hold_attempt(void *p)
{
    auto *a = static_cast<tl_arg *>(p);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += 200000000L;   // 200 ms
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    a->result = pthread_mutex_timedlock(a->m, &ts);
    return nullptr;
}

int main()
{
    std::cerr << "Running pthread timedlock tests\n";
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

    // Uncontended: timedlock succeeds immediately.
    struct timespec t = deadline_in(CLOCK_REALTIME, 1000);
    assert(pthread_mutex_timedlock(&m, &t) == 0);

    // Held by us: a second timedlock (on another thread) must time out rather
    // than deadlock.
    tl_arg a { &m, -999 };
    pthread_t th;
    pthread_create(&th, nullptr, hold_attempt, &a);
    pthread_join(th, nullptr);
    assert(a.result == ETIMEDOUT);   // could not acquire; we still hold it

    pthread_mutex_unlock(&m);

    // After unlock, timedlock succeeds again.
    t = deadline_in(CLOCK_REALTIME, 1000);
    assert(pthread_mutex_timedlock(&m, &t) == 0);
    pthread_mutex_unlock(&m);

    // clocklock with CLOCK_MONOTONIC: uncontended success.
    t = deadline_in(CLOCK_MONOTONIC, 1000);
    assert(pthread_mutex_clocklock(&m, CLOCK_MONOTONIC, &t) == 0);
    pthread_mutex_unlock(&m);

    // clocklock with an unsupported clock -> EINVAL.
    t = deadline_in(CLOCK_MONOTONIC, 1000);
    assert(pthread_mutex_clocklock(&m, 99, &t) == EINVAL);

    std::cerr << "pthread timedlock tests PASSED\n";
    return 0;
}
