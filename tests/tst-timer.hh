#ifndef __TST_TIMER__
#define __TST_TIMER__

#include "tst-hub.hh"
#include "drivers/clock.hh"
#include "debug.hh"

class test_timer : public unit_tests::vtest {

public:

    void run()
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
};

#endif
