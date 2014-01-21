/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __TST_THREADS__
#define __TST_THREADS__

#include "tst-hub.hh"
#include <osv/sched.hh>
#include <osv/debug.hh>

class test_threads : public unit_tests::vtest {

public:
    struct test_threads_data {
        sched::thread* main;
        sched::thread* t1;
        bool t1ok;
        sched::thread* t2;
        bool t2ok;
        int test_ctr;
    };

    void test_thread_1(test_threads_data& tt)
    {
        while (tt.test_ctr < 1000) {
            sched::thread::wait_until([&] { return (tt.test_ctr % 2) == 0; });
            ++tt.test_ctr;
            if (tt.t2ok) {
                tt.t2->wake();
            }
        }
        tt.t1ok = false;
        tt.main->wake();
    }

    void test_thread_2(test_threads_data& tt)
    {
        while (tt.test_ctr < 1000) {
            sched::thread::wait_until([&] { return (tt.test_ctr % 2) == 1; });
            ++tt.test_ctr;
            if (tt.t1ok) {
                tt.t1->wake();
            }
        }
        tt.t2ok = false;
        tt.main->wake();
    }

    void run()
    {
        test_threads_data tt;
        tt.main = sched::thread::current();
        tt.t1ok = tt.t2ok = true;
        tt.t1 = new sched::thread([&] { test_thread_1(tt); });
        tt.t2 = new sched::thread([&] { test_thread_2(tt); });
        tt.test_ctr = 0;
        tt.t1->start();
        tt.t2->start();
        sched::thread::wait_until([&] { return tt.test_ctr >= 1000; });
        tt.t1->join();
        tt.t2->join();
        delete tt.t1;
        delete tt.t2;
        debug("threading test succeeded\n");
    }
};

#endif
