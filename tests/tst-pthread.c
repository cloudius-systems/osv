/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <errno.h>

pthread_mutex_t mutex;
pthread_cond_t cond;
pthread_t thread;
unsigned data;
unsigned acks;

unsigned tests_total, tests_failed;
bool done;

void report(const char* name, bool passed)
{
    static const char* status[] = { "FAIL", "PASS" };
    printf("%s: %s\n", status[passed], name);
    tests_total += 1;
    tests_failed += !passed;
}

void wait_for_ack(unsigned n)
{
    pthread_mutex_lock(&mutex);
    while (acks != n) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);
}

void post_ack()
{
    ++acks;
    pthread_cond_signal(&cond);
}

void* secondary(void *ignore)
{
    struct timespec ts;
    struct timeval tv;
    int r;

    printf("starting secondary\n");
    pthread_mutex_lock(&mutex);
    r = 0;
    while (data != 1 && r == 0) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);
    report("pthread_cond_wait", r == 0);

    post_ack(1);

    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec + 1000000;
    ts.tv_nsec = 0;
    pthread_mutex_lock(&mutex);
    r = 0;
    while (data != 2 && r == 0) {
        r = pthread_cond_timedwait(&cond, &mutex, &ts);
    }
    pthread_mutex_unlock(&mutex);
    report("pthread_cond_timedwait (long)", r == 0);

    post_ack(2);

    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec;
    ts.tv_nsec = tv.tv_usec * 1000 + 1000000;
    pthread_mutex_lock(&mutex);
    r = 0;
    while (data != 666 && r == 0) {
        r = pthread_cond_timedwait(&cond, &mutex, &ts);
    }
    pthread_mutex_unlock(&mutex);
    report("pthread_cond_timedwait (short)", r == ETIMEDOUT);

    post_ack(3);

    done = true;
    pthread_cond_signal(&cond);
    return NULL;
}

int main(void)
{
    printf("starting pthread test\n");
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    pthread_create(&thread, NULL, secondary, NULL);

    pthread_mutex_lock(&mutex);
    data = 1;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);

    wait_for_ack(1);

    pthread_mutex_lock(&mutex);
    data = 2;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);

    wait_for_ack(2);

    wait_for_ack(3);

    // FIXME: join instead
    pthread_mutex_lock(&mutex);
    while (!done) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);
    void* ret;
    pthread_join(thread, &ret);

    // Test that we can configure pthread_cond_timedwait to use
    // CLOCK_MONOTONIC, and that it works as expected - we set a short but
    // nonzero timeout, and expect to reach it, but not immediately (since
    // the monotonic clock is much lower than the realtime clock, thinking
    // it is a realtime clock value will result in an immediate timeout).
    int r;
    pthread_mutex_t mutex2;
    pthread_cond_t cond2;
    pthread_condattr_t attr2;
    r = pthread_condattr_init(&attr2);
    report("pthread_condattr_init", r == 0);
    r= pthread_condattr_setclock(&attr2, CLOCK_MONOTONIC);
    report("pthread_condattr_setclock", r == 0);
    pthread_mutex_init(&mutex2, NULL);
    r = pthread_cond_init(&cond2, &attr2);
    report("pthread_cond_init", r == 0);
    r = pthread_condattr_destroy(&attr2);
    report("pthread_condattr_destroy", r == 0);
    struct timespec ts;
    r = clock_gettime(CLOCK_MONOTONIC, &ts);
    report("clock_gettime(CLOCK_MONOTONIC)", r == 0);
    struct timespec to = ts;
    to.tv_nsec += 500000000;
    pthread_mutex_lock(&mutex2);
    r = pthread_cond_timedwait(&cond2, &mutex2, &to);
    pthread_mutex_unlock(&mutex2);
    report("pthread_cond_timedwait (short)", r == ETIMEDOUT);
    struct timespec ts2;
    clock_gettime(CLOCK_MONOTONIC, &ts2);
    long ns = (ts2.tv_sec - ts.tv_sec)*1000000000 + (ts2.tv_nsec - ts.tv_nsec);
    report("should have not returned immediately", ns > 400000000);
    printf("ts  = %ld,%ld\n",ts.tv_sec, ts.tv_nsec);
    printf("ts2 = %ld,%ld\n",ts2.tv_sec, ts2.tv_nsec);
    printf("ns = %ld\n",ns);

    // Test that pthread_spin_unlock() doesn't crash when it is resolved for
    // the first time while a spinlock is taken (see issue #814)
    pthread_spinlock_t spin;
    pthread_spin_init(&spin, PTHREAD_PROCESS_PRIVATE);
    pthread_spin_lock(&spin);
    pthread_spin_unlock(&spin);
    pthread_spin_destroy(&spin);
    // Moreover, the application may even sleep while holding a spinlock
    pthread_spin_init(&spin, PTHREAD_PROCESS_PRIVATE);
    pthread_spin_lock(&spin);
    usleep(1000);
    pthread_spin_unlock(&spin);
    pthread_spin_destroy(&spin);

    printf("SUMMARY: %u tests / %u failures\n", tests_total, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
