/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Test std::thread's complete functionality() (and as a result, also pthread's, which
// std::thread uses internally). Threads need to be either attached or joined, and we
// will test them both. Our hope here is to create threads that are extremely short and
// will complete very rapidly, therefore racing with both detach and join. We will call
// attach and join in various orders, hoping that if there is a race, it will show up.
//
// Passing this test doesn't mean it works, but failing means it doesn't.
// Unfortunately there are no checks to be done here.  Failure will manifest in
// this crashing or hanging.

#include <sys/types.h>
#include <thread>
#include <iostream>
#ifdef __OSV__
#include "sched.hh"
#include "drivers/clock.hh"
#endif

void detach_or_join(std::thread& t, bool detach)
{
    if (detach) {
        t.detach();
    } else {
        t.join();
    }
}

void do_test(bool detach)
{
    // Create threads and wait for their completion
    // in the order they are created.
    for (int i = 0; i < 100; ++i) {
        std::thread t1([] { } );
        std::thread t2([] { } );
        std::thread t3([] { } );
        std::thread t4([] { } );
        detach_or_join(t1, detach);
        detach_or_join(t2, detach);
        detach_or_join(t3, detach);
        detach_or_join(t4, detach);
    }

    // Now in the inverse order.
    for (int i = 0; i < 100; ++i) {
        std::thread t1([] { } );
        std::thread t2([] { } );
        std::thread t3([] { } );
        std::thread t4([] { } );
        detach_or_join(t4, detach);
        detach_or_join(t3, detach);
        detach_or_join(t2, detach);
        detach_or_join(t1, detach);
    }

    // Complete a thread as soon as we create them.
    for (int i = 0; i < 100; ++i) {
        std::thread t1([] { } );
        detach_or_join(t1, detach);
        std::thread t2([] { } );
        detach_or_join(t2, detach);
        std::thread t3([] { } );
        detach_or_join(t3, detach);
        std::thread t4([] { } );
        detach_or_join(t4, detach);
    }

    // Complete one thread from the other's std::function.  This block is the
    // reason why I am capping this to 4 threads, since I want every thread to
    // exist in the same block so they are destroyed together. The others are 4
    // instead of ncpus for consistency.
    for (int i = 0; i < 100; ++i) {
        std::atomic<int> threads = {0};


        std::thread t1([&] { threads.fetch_add(1); } );
        std::thread t2([&] { detach_or_join(t1, detach); threads.fetch_add(1); } );
        std::thread t3([&] { detach_or_join(t2, detach); threads.fetch_add(1); } );
        std::thread t4([&] { detach_or_join(t3, detach); threads.fetch_add(1); } );
        detach_or_join(t4, detach);
        // All short lived, and for god's sake, we're just a test.
        while (threads.load() != 4);
    }
}

void do_heap_test(bool quick)
{
#ifdef __OSV__
    // This is the same code that runs in tst-mmap.cc, which is what I was
    // running when I noticed this bug. Being a race condition, it doesn't
    // happen all the time. I moved the code here, but without all the mmap
    // stuff (so only the thread bits are at play), and I am looping 100 times,
    // hoping that one of the iterations will end up in the tricky order that
    // triggers the race.
    for (int j = 0; j < 100; ++j) {
        sched::thread *t2 = nullptr;
        sched::thread *t1 = new sched::thread([&]{
            // wait for the t2 object to exist (not necessarily run)
            sched::thread::wait_until([&] { return t2 != nullptr; });
            if (quick) {
                return;
            }
            sched::thread::sleep_until(nanotime() + 10_ms);
        }, sched::thread::attr(sched::cpus[0]));

        t2 = new sched::thread([&]{
            t1->wake();
        }, sched::thread::attr(sched::cpus[1]));

        t1->start();
        t2->start();
        delete t2;
        delete t1;
    }
#endif
}

int main(int ac, char** av)
{
    std::cerr << "Starting detach tests...\n";
    do_test(true);
    std::cerr << "Starting join tests...\n";
    do_test(false);
    std::cerr << "Starting thread-on-heap tests... (no load)\n";
    do_heap_test(false);
    std::cerr << "Starting thread-on-heap tests... (loaded)\n";
    do_heap_test(true);
    std::cerr << "Passed\n";
}
