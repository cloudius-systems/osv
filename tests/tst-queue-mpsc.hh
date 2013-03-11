#ifndef __TST_QMPSC__
#define __TST_QMPSC__

#include "tst-hub.hh"
#include "sched.hh"
#include "debug.hh"
#include "lockfree/queue-mpsc.hh"

class test_queue_mpsc : public unit_tests::vtest {

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
        debug("Running lockfree multi-producer single-consumer queue tests");
        // Test trivial single-thread, queuing.
        debug("test1");
        lockfree::queue_mpsc<int> q(-1);
        assert(q.pop()==-1);
        assert(q.isempty());
        debug("test2");
        lockfree::linked_item<int> item(7);
        q.push(&item);
        assert(!q.isempty());
        assert(q.pop()==7);
        assert(q.pop()==-1);
        assert(q.isempty());

        debug("test3");

        int len=10;
        auto items = new lockfree::linked_item<int>[len];
        for(int i=0; i<len; i++){
            items[i].value = i*i;
            q.push(&items[i]);
        }
        for(int i=0; i<len; i++){
            assert(!q.isempty());
            assert(q.pop()==i*i);
        }
        assert(q.pop()==-1);
        assert(q.isempty());
        delete[] items;

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
        debug("lockfree MPSC queue tests succeeded");
    }
};

#endif
