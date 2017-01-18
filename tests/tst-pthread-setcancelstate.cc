/*
 * Copyright (C) 2016 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Test pthread_setcancelstate()
 *
 * To run the test on Linux:
 * Step 1: g++ -g -pthread -std=c++11 tests/tst-pthread-setcancelstate.cc
 * Step 2: a.out
 *
 * To run the test on OSv:
 * Step 1: scripts/build image=tests
 * Step 2: scripts/run.py -e tests/tst-pthread-setcancelstate.so
 */

#include <cassert>
#include <errno.h>
#include <iostream>
#include <string.h>
#include <thread>

#define INVALID_STATE 100
#define DEFAULT_STATE PTHREAD_CANCEL_ENABLE

void thread_payload(int thread_id) {
    int old_state, retval;

    // Test 1: Verify default cancel state is PTHREAD_CANCEL_ENABLE.
    retval = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
    assert((retval == 0) &&
           (old_state == DEFAULT_STATE));
    printf("Thread %d: Verified default cancel state is %d\n",
           thread_id, old_state);

    // Test 2: Verify target cancel state from Test 1 persisted.
    retval = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
    assert((retval == 0) &&
           (old_state == PTHREAD_CANCEL_DISABLE));
    printf("Thread %d: Verified persistence of set cancel state (%d)\n",
           thread_id, old_state);

    // Test 3: Attempt to set cancel state to a value that is neither
    // PTHREAD_CANCEL_ENABLE nor PTHREAD_CANCEL_DISABLE returns
    // EINVAL.
    retval = pthread_setcancelstate(INVALID_STATE, &old_state);
    assert(retval == EINVAL);
    printf("Thread %d: Attempt to set invalid cancel state (%d) returned %d\n",
           thread_id, INVALID_STATE, retval);
}

int main(int argc, char **argv)
{
    printf("Testing pthread_setcancelstate..\n");
    std::thread thread1( thread_payload, 1 );
    std::thread thread2( thread_payload, 2 );
    std::thread thread3( thread_payload, 3 );
    thread1.join();
    thread2.join();
    thread3.join();
    printf("Testing completed.\n");
}
