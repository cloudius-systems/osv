/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef TST_SYNCH_H
#define TST_SYNCH_H

#include <osv/sched.hh>
#include <osv/debug.hh>
#include "tst-hub.hh"

#include <bsd/porting/netport.h>
extern "C" {
    #include <bsd/porting/synch.h>
}

#define dbg_d(...) tprintf_d("tst-synch", __VA_ARGS__)

class test_synch : public unit_tests::vtest {
public:
    test_synch() {}
    virtual ~test_synch() {}

    //
    // Create 2 threads that wait for an event
    // Release them one by one
    //
    void test2(void)
    {
        dbg_d("test2 - start");

        sched::thread::attr a;
        a.detached();
        sched::thread *t21 = sched::thread::make([this] {
            dbg_d("t21-before");
            msleep((void*)567, NULL, 0, "test1", 0);
            dbg_d("t21-after");
        }, a);

        sched::thread *t22 = sched::thread::make([this] {
            dbg_d("t22-before");
            msleep((void*)567, NULL, 0, "test1", 0);
            dbg_d("t22-after");
        }, a);

        t21->start();
        t22->start();

        for (int i=0; i<2; i++) {
            /* Wait on another channel, with a timeout... */
            dbg_d("test1 - waiting on event 234 with a timeout of 1 second");
            msleep((void*)888, NULL, 0, "test1", 1 * hz);
            dbg_d("test1 - releasing event 123");
            wakeup_one((void*)567);
        }

        dbg_d("test2 - end");
    }

    //
    // Create 2 threads that wait for an event
    // then block the main thread (wait for another event with timeout)
    // then signal the first event
    //
    void test1(void)
    {
        dbg_d("test1 - start");

        sched::thread::attr a;
        a.detached();
        sched::thread *t11 = sched::thread::make([this] {
            dbg_d("t11-before");
            msleep((void*)123, NULL, 0, "test1", 0);
            dbg_d("t11-after");
        }, a);

        sched::thread *t12 = sched::thread::make([this] {
            dbg_d("t12-before");
            msleep((void*)123, NULL, 0, "test1", 0);
            dbg_d("t12-after");
        }, a);

        t11->start();
        t12->start();

        /* Wait on another channel, with a timeout... */
        dbg_d("test1 - waiting on event 234 with a timeout of 1 second");
        msleep((void*)234, NULL, 0, "test1", 1 * hz);
        dbg_d("test1 - releasing event 123");
        wakeup((void*)123);

        dbg_d("test1 - end");
    }

    void run(void)
    {
#if 0
        // Run the tests in detached threads
        sched::thread::attr a;
        a.detached();
        sched::thread *t1 = sched::thread::make([this] { test1(); }, a);
        t1->start();

        sched::thread *t2 = sched::thread::make([this] { test2(); }, a);
        t2->start();
#endif
    }

};

#undef dbg_d

#endif
