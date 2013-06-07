#ifndef __TST_RWLOCK__
#define __TST_RWLOCK__

#include <osv/debug.h>
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


#define rw_tag "tst-rwlock"
#define rw_d(...)   tprintf_d(rw_tag, __VA_ARGS__)

class test_rwlock : public unit_tests::vtest {
public:

    // Test context
    thread* _main;
    bool _test1_finished;

    rwlock _test2_rwlock;
    bool _test2_t1_finished;
    bool _test2_t2_finished;

    bool _test3_finished;

    void rwlock_test3(void)
    {
        rw_d("rwlock_test3... ");

        rwlock lock;
        rw_init(&lock, "tst3");

        if (rw_try_rlock(&lock)) {
            rw_runlock(&lock);
        }

        rw_rlock(&lock);

        if (rw_try_rlock(&lock)) {
            rw_runlock(&lock);
        }

        rw_runlock(&lock);

        rw_d("OK");

        _test3_finished = true;
        _main->wake();
    }

    void rwlock_test2_t1(void)
    {
        for (int i=0; i<10; i++) {
            rw_d("T1 Locking...");
            rw_rlock(&_test2_rwlock);
            rw_d("T1 Locked");
            rw_d("  rwlock_test2_t1");
            thread::current()->yield();
            rw_d("T1 Unlocked");
            rw_runlock(&_test2_rwlock);
        }
        _test2_t1_finished = true;
        _main->wake();
    }

    void rwlock_test2_t2(void)
    {
        for (int i=0; i<5; i++) {
            rw_d("T2 Locking...");
            rw_wlock(&_test2_rwlock);
            rw_d("T2 Locked");
            rw_d("  rwlock_test2_t2");
            rw_d("T2 Unlocked");
            rw_wunlock(&_test2_rwlock);
            thread::current()->yield();
        }
        _test2_t2_finished = true;
        _main->wake();
    }

    void rwlock_test1(void)
    {
        rw_d("rwlock_test1... ");

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

        rw_d("OK");

        _test1_finished = true;
        _main->wake();
    }

    void run()
    {
        rw_d("Testing rwlock:");

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

        // Test 3
        _test3_finished = false;
        thread* t3 = new thread([&] { rwlock_test3(); });
        t3->start();
        _main->wait_until([&] { return (_test3_finished); });
        delete t3;
    }
};

#endif
