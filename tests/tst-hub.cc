/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "tst-hub.hh"
#include "tst-threads.hh"
#include "tst-malloc.hh"
#include "tst-timer.hh"
#include "tst-devices.hh"
#include "tst-eventlist.hh"
#include "tst-rwlock.hh"
#include "tst-bsd-synch.hh"

using namespace unit_tests;

void tests::execute_tests() {
    test_threads threads;
    test_malloc malloc;
    test_timer timer;
    test_devices dev;
    test_eventlist evlist;
    test_rwlock rwlock;
    test_synch synch;

    instance().register_test(&threads);
    instance().register_test(&malloc);
    instance().register_test(&timer);
    instance().register_test(&dev);
    instance().register_test(&evlist);
    instance().register_test(&rwlock);
    instance().register_test(&synch);

    instance().run();
}

int main(int ac, char** av)
{
    unit_tests::tests::instance().execute_tests();
    return 0;
}

