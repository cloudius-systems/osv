/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Use this benchmark to measure the time slice that two CPU-hog threads
// get when competing against each other, on OSv and on Linux, so we can
// callibrate OSv's behavior in this case to be similar to Linux's.
//
// Please run this benchmark on a single CPU.
// To run this benchmark on Linux:
//    g++ -g -pthread -std=c++11 tests/misc-timeslice.cc
//    taskset 1 ./a.out
// To run on osv:
//    make image=tests
//    scripts/run.py -c1 -e tests/misc-timeslice.so

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <atomic>

#include <sys/resource.h>
#include <unistd.h>
#include <stdio.h>


void timeslice_test()
{
    std::atomic<int> shared {0};
    std::atomic<bool> done {false};
    std::atomic<int> cs {0};
    auto func = [&](int id) {
        while (!done.load(std::memory_order_relaxed)) {
            if (shared.load(std::memory_order_relaxed) != id) {
                shared.store(id, std::memory_order_relaxed);
                cs.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };
    std::thread t1([&] { func(1); });
    std::thread t2([&] { func(2); });
    sleep(10);
    done = true;
    t1.join();
    t2.join();
    std::cout << "In 10 seconds, " << cs << " context switches\n";
    std::cout << "Average timeslice: " << (10.0 * 1000 / cs) << "ms\n";
}
int main()
{
    timeslice_test();
}
