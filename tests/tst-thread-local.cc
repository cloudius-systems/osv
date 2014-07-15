/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Tests the C++11 "thread_local" feature.
// thread_local is similar to gcc's older "__thread", except it allows
// a constructor and destructor on the thread-local object.
//
// To compile on Linux, use: g++ -g -pthread -std=c++11 tst-thread-local.cc


#include <string>
#include <iostream>
#include <thread>
#include <atomic>

#include <stdio.h>
#include <string.h>

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

std::atomic<int> constructions(0), destructions(0);

class test_class {
public:
    test_class() {
        std::cout << "constructor\n";
        constructions++;
    }
    ~test_class() {
        std::cout << "destructor\n";
        destructions++;
    }
    int val;
};

thread_local test_class tlobj;
thread_local std::string tlobj2;


int main(int ac, char** av)
{
    report(tlobj.val == 0, "default value on main thread");
    tlobj.val=1;
    report(tlobj.val == 1, "original value on main thread");
    std::thread t1([] {
            tlobj.val = 2;
            report(tlobj.val == 2, "2 in second thread");
    });
    report(tlobj.val == 1, "original value on main thread");
    t1.join();
    report(tlobj.val == 1, "original value on main thread");
    report(constructions == 2, "2 constructions");
    report(destructions == 1, "1 destructions");

    // Similar, with std::string. See we don't crash.
    report(tlobj2 == "", "default value on main thread");
    tlobj2="yo";
    report(tlobj2 == "yo", "original value on main thread");
    std::thread t2([] {
            report(tlobj2 == "", "default value in second thread");
            tlobj2 = "hey";
            report(tlobj2 == "hey", "hey in second thread");
    });
    report(tlobj2 == "yo", "original value on main thread");
    t2.join();
    report(tlobj2 == "yo", "original value on main thread");

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
}
