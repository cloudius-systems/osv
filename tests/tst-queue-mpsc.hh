#ifndef __TST_QMPSC__
#define __TST_QMPSC__

#include "tst-hub.hh"
#include "sched.hh"
#include "debug.hh"
#include "lockfree/queue-mpsc.hh"

class test_queue_mpsc : public unit_tests::vtest {

public:
    struct info {
        lockfree::queue_mpsc<int> *q;
        int len;
        lockfree::linked_item<int> *items;
    };
    void push_thread(struct info *in)
    {
        for(int i=0; i<in->len; i++){
            in->items[i].value = i;
            in->q->push(&in->items[i]);
        }
    }

    void pop_thread(struct info *in)
    {
        int sum=0, something=0, nothing=0;
        while(something<in->len){
            int n = in->q->pop();
            if(n<0)
                nothing++;
            else {
                something++;
                sum+=n;
            }
        }

        debug(fmt("pop_thread saw something %d times, nothing %d times. sum: %d") % something % nothing % sum);
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
        int len=1000;
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
        // A very basic multi-threaded test - 3 threads pushing, 1 popping
        len=1000;
        auto items1 = new lockfree::linked_item<int>[len];
        auto items2 = new lockfree::linked_item<int>[len];
        auto items3 = new lockfree::linked_item<int>[len];
        struct info info1 = { &q, len, items1 };
        struct info info2 = { &q, len, items2 };
        struct info info3 = { &q, len, items3 };
        struct info infop = { &q, len*3, nullptr };
        auto t1 = new sched::thread([&] { push_thread(&info1); });
        auto t2 = new sched::thread([&] { push_thread(&info2); });
        auto t3 = new sched::thread([&] { push_thread(&info3); });
        auto tp = new sched::thread([&] { pop_thread(&infop); });
        tp->start();
        t1->start();
        t2->start();
        t3->start();
        t1->join();
        t2->join();
        t3->join();
        tp->join();
        debug(fmt("sum should be %d") % ((len-1)*len/2 * 3));
        delete t1;
        delete t2;
        delete t3;
        delete tp;
        delete[] items1;
        delete[] items2;
        delete[] items3;
        debug("lockfree MPSC queue tests succeeded");
    }
};

#endif
