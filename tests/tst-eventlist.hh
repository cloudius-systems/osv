/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef TST_EVENTLIST_H
#define TST_EVENTLIST_H

#include "eventlist.hh"
#include "debug.hh"
#include "tst-hub.hh"

#define dbg_d(...)   tprintf_d("tst-eventlist", __VA_ARGS__)

class test_eventlist : public unit_tests::vtest {
public:
    test_eventlist() {}
    virtual ~test_eventlist() {}

    void handler1(void)
    {
        dbg_d("handler1");
    }

    void handler2(void)
    {
        dbg_d("handler2");
    }

    void handler3(void)
    {
        dbg_d("handler3");
    }

    void handler4(void)
    {
        dbg_d("handler4");
    }

    void handler5(void)
    {
        dbg_d("handler5");
    }

    void run(void)
    {
        // create 2 events
        event_manager->create_event("event_a");
        event_manager->create_event("event_b");

        // register handler 1&2 to event a
        int h1 = event_manager->register_event("event_a", [&] { handler1(); });
        int h2 = event_manager->register_event("event_a", [&] { handler2(); });
        // register handler 3&4 to event b
        int h3 = event_manager->register_event("event_b", [&] { handler3(); });
        int h4 = event_manager->register_event("event_b", [&] { handler4(); });

        // invoke event a & b
        event_manager->invoke_event("event_a");
        event_manager->invoke_event("event_b");

        // remove handler 1 & append handler 5
        event_manager->deregister_event("event_a", h1);
        int h5 = event_manager->register_event("event_a", [&] { handler5(); });

        // invoke event a & b
        event_manager->invoke_event("event_a");
        event_manager->invoke_event("event_b");

        event_manager->deregister_event("event_a", h2);
        event_manager->deregister_event("event_a", h5);
        event_manager->deregister_event("event_b", h3);
        event_manager->deregister_event("event_b", h4);
    }
};

#undef dbg_d

#endif
