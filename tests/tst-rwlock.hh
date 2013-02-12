#ifndef __TST_RWLOCK__
#define __TST_RWLOCK__

#include "debug.hh"
#include "sched.hh"
#include "tst-hub.hh"

using namespace sched;

extern "C" {
    #include <bsd/porting/rwlock.h>
}

class test_rwlock : public unit_tests::vtest {
public:

    // Test context
    thread* _main;
    bool _test1_finished;

    // test a basic flow withing a single thread...
    void rwlock_test1(void)
    {
        debug("rwlock_test1... ", false);

        rwlock lock;
        rw_init(&lock, "tst1");

        rw_rlock(&lock);
        rw_wlock(&lock);
        rw_wunlock(&lock);

        rw_wlock(&lock);
        rw_wunlock(&lock);

        rw_wlock(&lock);
        rw_wunlock(&lock);

        rw_runlock(&lock);

        debug("OK");

        _test1_finished = true;
        _main->wake();
    }

    void run()
    {
        debug("Testing rwlock:");

        // Init
        _main = thread::current();
        _test1_finished = false;

        // Run test1 in a thread
        thread* t1 = new thread([&] { rwlock_test1(); });

        // Join...
        _main->wait_until([&] { return (_test1_finished); });
        delete t1;
    }
};

#endif
