/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Use this benchmark to calibrate setpriority() to have a similar effect
// to what it does in Linux. I.e., when a thread that does setpriority(10)
// competes with a thread with default priority (0), what is the runtime
// ratio they get? This benchmark can run on both OSv and Linux.
//
// In OSv's internal API, thread::setpriority() has a very well-defined
// meaning: A thread with OSv priority "x" competing against a thread
// with OSv priority 1 will get 1/x times the runtime. In the POSIX
// setpriority() API, no such definition exists, and the amount of CPU
// time that a thread with certain nice value will get varied between
// Unix variants, and Linux versions. The Linux setpriority(2) manual
// explains that starting with Linux 2.6.23, the effect of setpriority()
// has become more extreme (low-priority threads now get much less
// run time), and with this benchmark we can measure exactly how.
//
// To run this benchmark on Linux:
//    g++ -g -pthread -std=c++11 tests/misc-setpriority.cc
//    sudo taskset 1 ./a.out

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <atomic>

#include <sys/resource.h>
#include <unistd.h>
#include <stdio.h>

void _loop(int iterations)
{
    for (register int i=0; i<iterations; i++) {
        for (register int j=0; j<10000; j++) {
            // To force gcc to not optimize this loop away
            asm volatile("" : : : "memory");
        }
    }
}

double priority_test(int nice)
{
    std::atomic<int> started {0};
    std::atomic<bool> done {false};
    std::atomic<bool> started_1;
    std::atomic<bool> started_n;
    int result_1, result_n;
    std::thread t1([&] {
        // Thread with priority 1
        started_1 = true;
        while (!started_n) {
            sched_yield();
        }
        int it = 0;
        while (!done) {
            _loop(1);
            it++;
        }
        result_1 = it;
    });
    std::thread tn([&] {
        // Thread with priority "nice"
        if(setpriority(PRIO_PROCESS, 0, nice) != 0) {
            perror("setpriority");
            exit(1);
        }
        started_n = true;
        while (!started_1) {
            sched_yield();
        }
        int it = 0;
        while (!done) {
            _loop(1);
            it++;
        }
        result_n = it;
    });


    sleep(10);
    done = true;
    t1.join();
    tn.join();

    double result = (double)result_n / result_1;
    return result;
}
int main()
{
    std::cout << "# nice-value runtime-ratio\n";
    for (int nice = -20; nice < 20; nice++) {
        std::cout << nice << "\t";
        std::cout.flush();
        std::cout << priority_test(nice) << "\n";
        std::cout.flush();
    }
    return 0;
}
