/*
 * Copyright (C) 2016 ScyllaDB Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Benchmark the performance of a contended mutex held for short durations.
//
// This test can be run on both Linux and OSv to compare their performance:
//    For OSv:
//       scripts/build image=tests,httpserver
//       scripts/run.py --api -e 'tests/misc-mutex2.so 2 500'
//    For Linux:
//       c++ -O2 --std=c++11 tests/misc-mutex2.cc -pthread
//       ./a.out 2 500
//
// In some applications, a mutex is held for a very short time - e.g., just
// to increment a counter. If this mutex is contended by several threads,
// some will go to sleep, incurring a context switch cost which is
// significantly higher than the cost to spin a bit waiting the the mutex
// to be released.
//
// Well-written modern applications should not hold a mutex to increment a
// counter, because they could use atomic operations, or even per-thread
// counters, instead of the mutex-protected counter. Nevertheless, some
// applications do use mutexes held for very short durations, and we want to
// check with this test how well (or badly) we handle this case compared to
// Linux. An example of such use case is the "sysbench --test=memory"
// benchmark.
//
// This test takes two parameters: the number of threads, and a computation
// length; Each of the parallel threads loops trying to take the mutex and
// increment a shared counter and then do some short computation of the
// specified length outside the loop. The test runs for 30 seconds, and
// shows the average number of lock-protected counter increments per second.
// The reason for doing some computation outside the lock is that makes the
// benchmark more realistic, reduces the level of contention and makes it
// beneficial for the OS to run the different threads on different CPUs:
// Wwithout any computation outside the lock, the best performance will be
// achieved by running all the threads.
//
// For example:
//    ./a.out 1 0
//    ./a.out 1 500
//    ./a.out 4 500

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <mutex>

void loop(int iterations)
{
    for (register int i=0; i<iterations; i++) {
        // To force gcc to not optimize this loop away
        asm volatile("" : : : "memory");
    }
}

int main(int argc, char** argv) {
    if (argc <= 2) {
        std::cerr << "Usage: " << argv[0] << " nthreads worklen\n";
        return 1;
    }
    int nthreads = atoi(argv[1]);
    if (nthreads <= 0) {
        std::cerr << "Usage: " << argv[0] << " nthreads worklen\n";
        return 2;
    }
    // "worklen" is the amount of work to do in each loop iteration, outside
    // the mutex. This reduces contention and makes the benchmark more
    // realistic and gives it the theoretic possibility of achieving better
    // benchmark numbers on multiple CPUs (because this "work" is done in
    // parallel.
    int worklen = atoi(argv[2]);
    if (worklen < 0) {
        std::cerr << "Usage: " << argv[0] << " nthreads worklen\n";
        return 3;
    }

    std::cerr << "Running " << nthreads << " threads on " <<
            std::thread::hardware_concurrency() << " cores. Worklen = " <<
            worklen << "\n";

    // Set secs to the desired number of seconds a measurement should
    // take. Note that the whole test will take several times longer than
    // secs, as we do several tests each lasting at least this long.
    double secs = 30.0;

    // Our mutex-protected operation will be a silly increment of a counter,
    // taking a tiny amount of time, but still can happen concurrently if
    // run very frequently from many cores in parallel.
    std::mutex mut;
    int counter = 0;
    bool done = false;

    std::vector<std::thread> threads;
    for (int i = 0; i < nthreads; i++) {
            threads.push_back(std::thread([&]() {
                while (!done) {
                    mut.lock();
                    counter++;
                    mut.unlock();
                    loop(worklen);
                }
            }));
    }
    threads.push_back(std::thread([&]() {
        std::this_thread::sleep_for(std::chrono::duration<double>(secs));
        done = true;
    }));
    for (auto &t : threads) {
        t.join();
    }
    std::cout << counter << " counted in " << secs << " seconds (" << (counter/secs) << " per sec)\n";

    return 0;
}
