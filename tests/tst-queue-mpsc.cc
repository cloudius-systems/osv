/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This is a test of the multi-producer single-consumer queue implementation
// of <lockfree/queue-mpsc.hh>.
//
// Instead of being written in osv-specific thread apis (sched::thread,
// mutex, condvar, etc.) this test is written using C++11's threading support
// (std::thread, std::condition_variable, etc.). This has two advantages:
//
// 1. It also makes this test double as a test for our support of the pthread
//    features needed by libstdc++ to implement. This test helped uncover a
//    bunch of bugs and missing features in our pthread implementation.
//
// 2. It allows running this test also on Linux, not just OSV. This allows,
//    if the test fails, to verify it also fails on Linux and therefore is a
//    real bug in the algorithm and not a bug in osv's thread or
//    synchronization mechanisms. To compile this test in Linux, run:
//    g++ -g -std=c++11 -DLINUX -I ../include tst-queue-mpsc.cc -lstdc++ -lpthread

#include "lockfree/queue-mpsc.hh"

#include <thread>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <vector>

#ifdef LINUX
void assert(bool c) {
    if(!c) {
        std::cerr << "assertion failed\n";
        abort();
    }
}
#else
#include <osv/debug.hh>
#endif

#include <osv/sched.hh> // debugging
int main(int argc, char **argv) {
    std::cerr << "Running lockfree multi-producer single-consumer queue tests\n";

    // Test trivial single-thread, queuing.
    std::cerr << "test1\n";
    lockfree::queue_mpsc<lockfree::linked_item<int>> q;
    assert(!q.pop());
    assert(q.empty());
    std::cerr << "test2\n";
    lockfree::linked_item<int> item(7);
    q.push(&item);
    assert(!q.empty());
    lockfree::linked_item<int> *n;
    n = q.pop();
    assert(n && n->value == 7);
    assert(!q.pop());
    assert(q.empty());
    std::cerr << "test3\n";
    int len = 1000;
    auto items = new lockfree::linked_item<int>[len];
    for (int i = 0; i < len; i++) {
        items[i].value = i * i;
        q.push(&items[i]);
    }
    for (int i = 0; i < len; i++) {
        assert(!q.empty());
        n = q.pop();
        assert(n && n->value == i * i);
    }
    assert(!q.pop());
    assert(q.empty());
    delete[] items;

    // This is not really relevant to this test, but we need to test it
    // somewhere :-) if __gthread_active_p() is 0, something is wrong with
    // our pthread implementation (in particular - pthread_cancel() doesn't
    // exist).
    assert(__gthread_active_p());

    // A simple multi-threaded test: A bunch of pusher threads push newly
    // allocated items (no flow control - the queue can grow very large)
    // and a popper pops them. To ensure there's some concurrency, all threads
    // wait to start at roughly the same time.
    std::cerr << "test4\n";
    lockfree::queue_mpsc<lockfree::linked_item<int>> waitq4;
    constexpr int npushers4 = 20;
    constexpr int niter4 = 10000;
    std::thread *pushers4[npushers4];
    std::vector<lockfree::linked_item<int>*> *tofree[npushers4];
    std::mutex mutex;
    std::condition_variable condvar;
    int ready=0;
    for (int i = 0; i < npushers4; i++) {
        //std::cerr << "Starting pusher " << i << "\n";
        tofree[i] = new std::vector<lockfree::linked_item<int>*>;
        pushers4[i] = new std::thread([i, &waitq4, &mutex, &condvar, &ready, &tofree] {
            // Wait for everybody to be ready
            std::unique_lock<std::mutex> lock(mutex);
            ready++;
            if (ready == npushers4)
                condvar.notify_all();
            else
                while(ready < npushers4) {
                    condvar.wait(lock);
                }
            lock.unlock();
            // Now run the real test:
            for(int j=0; j<niter4; j++) {
                auto item = new lockfree::linked_item<int>(i*niter4+j);
                waitq4.push(item);
                tofree[i]->push_back(item);
            }
        });
    }
    std::thread *popper4 = new std::thread([&waitq4,&condvar,&mutex, &ready] {
        // Wait for everybody to be ready
        std::unique_lock<std::mutex> lock(mutex);
        while(ready < npushers4) {
            condvar.wait(lock);
        }
        lock.unlock();
        // Now run the real test:
        int left = npushers4*niter4;
        while (left) {
            lockfree::linked_item<int> *item;
            if ((item = waitq4.pop())) {
                // TODO: this test leaks all the item memory! :-) See below for a better flow-controlled test
                // another option is to add a list<LT*> and free it all when we're done with this test.
                left--;
            }
        }
    });
    popper4->join();
    delete popper4;
    for (int i = 0; i < npushers4; i++) {
        pushers4[i]->join();
    }
    for (int i = 0; i < npushers4; i++) {
        for (auto item : *tofree[i]) {
            delete item;
        }
        delete tofree[i];
        delete pushers4[i];
    }

    // We can't run the above test for very long, because it keeps allocating
    // memory which we don't free until the end. This one adds very rudimentary
    // flow-control: A pusher doesn't push any more until it is told (via a
    // condtion variable) that its previously added item was freed. This also
    // serves as a test for condition variables.
    // In this test, a bunch of "pusher" threads each pushes its own item, with
    // a sequence number, and waits until a single "popper" thread (always the
    // same thread in this test) pops the item and awakes the pusher. The
    // popper thread verifies that it gets all the expected items, in their
    // right order.
    std::cerr << "test5\n";
    constexpr int npushers = 20;
    constexpr int niter = 100000;
    struct waititem {
        int waiter;
        int *wakeup;
        int payload;
        int debugflag; // for debugging
    };
    lockfree::queue_mpsc<lockfree::linked_item<struct waititem>> waitq;
    std::thread *pushers[npushers];
    std::mutex mutexes[npushers];
    std::condition_variable condvars[npushers];
    for (int i = 0; i < npushers; i++) {
        //std::cerr << "Starting pusher " << i << "\n";
        pushers[i] = new std::thread([i, &waitq, &mutexes, &condvars] {
            lockfree::linked_item<struct waititem> item;
            int wakeup=1; // initialized as debugging aid
            item.value.waiter = i;
            item.value.wakeup = &wakeup;
            item.value.debugflag = 17;
            for (int counter = 0; counter < niter; counter++) {
                assert(counter==0 || item.value.payload == (counter-1)); // sanity check...
                item.value.payload = counter;
                assert(wakeup==1);// debugging try
                assert(item.value.debugflag==17);// debugging
                item.value.debugflag=57;// debugging
                wakeup = 0;
                waitq.push(&item);
                std::unique_lock<std::mutex> lock(mutexes[i]);
                while (!wakeup) {
                    condvars[i].wait(lock);
                }
                assert(item.value.debugflag==57);
                item.value.debugflag = 17;
                lock.unlock();
                assert(item.value.payload == counter);// sanity check...
            }
        });
    }
    std::thread *popper =
        new std::thread([&waitq,&condvars,&mutexes] {
        int nnothing=0, nsomething=0;
        int left = npushers;
        int last[npushers];
        for(int j = 0; j < npushers; j++)
            last[j] = -1;
        while (left) {
            struct lockfree::linked_item<waititem> *item;
            if ((item = waitq.pop())) {
                assert(item->value.waiter!=-1); // debugging
                assert(item->value.wakeup!=nullptr);//debugging
                assert(item->value.debugflag==57);// debugging
                nsomething++;
                if (!(nsomething%10000)) {
                    std::cerr << ".";
                }
                assert(item->value.waiter >= 0 && item->value.waiter < npushers);
                assert(item->value.payload == (last[item->value.waiter]+1));
                last[item->value.waiter]++;
                if (last[item->value.waiter] == niter-1) {
                    left--; // this pusher is done.
                }
                // It's important to take the lock when setting wakeup=1. If
                // we didn't, we might set wakeup=1 (and then notify_one())
                // right after the other thread tested it, just before it
                // began waiting.
                auto waiter = item->value.waiter;
                mutexes[waiter].lock();
                assert(*(item->value.wakeup)==0);// sanity check
                *(item->value.wakeup) = 1;
                mutexes[waiter].unlock();
                condvars[waiter].notify_one();
            } else {
                nnothing++;
            }
        }
        std::cerr << "\ndone with nsomething=" << nsomething << " nnothing=" << nnothing << "\n";
    });
    assert(popper->joinable());
    popper->join();
    assert(!popper->joinable());
    delete popper;
    for (int i = 0; i < npushers; i++)
        pushers[i]->join();
    for (int i = 0; i < npushers; i++)
        delete pushers[i];
    std::cerr << "lockfree MPSC queue tests succeeded\n";
    return 0;
}
