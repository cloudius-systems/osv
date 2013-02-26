#ifndef __TST_RWLOCK__
#define __TST_RWLOCK__

#include "debug.hh"
#include "sched.hh"
#include "tst-hub.hh"

using namespace sched;

extern "C" {
    #include <bsd/porting/rwlock.h>
}

//
// Test 1 - test lock recursively on the same thread
// Test 2 - test sleep of rwlock from 2 threads, read/write equivalence
//          The key is in the call to thread::current()->yield() before
//          releasing the lock
//


class test_rwlock : public unit_tests::vtest {
public:

    // Test context
    thread* _main;
    bool _test1_finished;

    rwlock _test2_rwlock;
    bool _test2_t1_finished;
    bool _test2_t2_finished;

    void rwlock_test2_t1(void)
    {
        for (int i=0; i<10; i++) {
            debug("T1 Locking...");
            rw_rlock(&_test2_rwlock);
            debug("T1 Locked");
            debug("  rwlock_test2_t1");
            thread::current()->yield();
            debug("T1 Unlocked");
            rw_runlock(&_test2_rwlock);
        }
        _test2_t1_finished = true;
        _main->wake();
    }

    void rwlock_test2_t2(void)
    {
        for (int i=0; i<5; i++) {
            debug("T2 Locking...");
            rw_wlock(&_test2_rwlock);
            debug("T2 Locked");
            debug("  rwlock_test2_t2");
            debug("T2 Unlocked");
            rw_wunlock(&_test2_rwlock);
            thread::current()->yield();
        }
        _test2_t2_finished = true;
        _main->wake();
    }

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

        rw_rlock(&lock);
        rw_runlock(&lock);

        rw_wlock(&lock);
        rw_wunlock(&lock);

        rw_runlock(&lock);

        rw_destroy(&lock);

        debug("OK");

        _test1_finished = true;
        _main->wake();
    }

    void run()
    {
        debug("Testing rwlock:");

        // Init
        _main = thread::current();

        // Test 1
        _test1_finished = false;
        thread* t1 = new thread([&] { rwlock_test1(); });
        t1->start();
        _main->wait_until([&] { return (_test1_finished); });
        delete t1;

        // Test 2
        _test2_t1_finished = false;
        _test2_t2_finished = false;
        rw_init(&_test2_rwlock, "tst2");
        thread* t2_1 = new thread([&] { rwlock_test2_t1(); });
        thread* t2_2 = new thread([&] { rwlock_test2_t2(); });
        t2_1->start();
        t2_2->start();
        _main->wait_until([&] { return (_test2_t1_finished && _test2_t2_finished); });
        delete t2_1;
        delete t2_2;
    }
};

#endif
