/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __TST_TIMER__
#define __TST_TIMER__

#include "tst-hub.hh"
#include "drivers/clock.hh"
#include "debug.hh"
#include "sched.hh"

class test_timer : public unit_tests::vtest {

public:

    void test1(void)
    {
        auto t1 = clock::get()->time();
        auto t2 = clock::get()->time();
        debug("Timer test: clock@t1 %1%\n", t1);
        debug("Timer test: clock@t2 %1%\n", t2);

        timespec ts = {};
        ts.tv_nsec = 100;
        t1 = clock::get()->time();
        nanosleep(&ts, nullptr);
        t2 = clock::get()->time();
        debug("Timer test: nanosleep(100) -> %d\n", t2 - t1);
        ts.tv_nsec = 100000;
        t1 = clock::get()->time();
        nanosleep(&ts, nullptr);
        t2 = clock::get()->time();
        debug("Timer test: nanosleep(100000) -> %d\n", t2 - t1);
    }

    static const int max_testers = 5000;
    static const int tester_iteration = 10;

    void stress_thread(void)
    {
        srandom(clock::get()->time());

        for (int i=0; i<tester_iteration; i++) {
            u64 ns = (random() % 1_s) - 500_ms;
            sched::timer t(*sched::thread::current());
            t.set(clock::get()->time() + ns);

            sched::thread::wait_until([&] { return (t.expired()); });
        }
    }


    // stress test the timer class
    void test2(void)
    {
        debug("Starting stress test\n");
        for (int i=0; i<max_testers; i++) {
            _testers[i] = new sched::thread([&] { this->stress_thread(); });
            _testers[i]->start();
        }

        // join
        for (int i=0; i<max_testers; i++) {
            _testers[i]->join();
            delete _testers[i];
        }
        debug("End stress test\n");
    }

    sched::thread *_testers[max_testers];

    void run()
    {
        test1();
        test2();
    }
};

#endif
