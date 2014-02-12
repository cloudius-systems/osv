/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Test concurrent initialization of function-static varibles.
// Gcc implements this using __cxa_guard_acquire().

#include <iostream>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <atomic>
#include <cassert>

using namespace std;

static int tests = 0, fails = 0;

static void report(bool ok, string msg)
{
    ++tests;
    fails += !ok;
    cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

// A type with a really slow constructor
static atomic<int> constructions {0};
struct mycounter {
    mycounter() {
        // This is a really slow constructor
        cout << "constructing\n";
        ++constructions;
        this_thread::sleep_for(chrono::seconds(1));
    }
    atomic<int> counter {};
    void incr() {
        counter++;
    }
    int get() {
        return counter.load();
    }
};

// A function using a static variable with a really slow constructor.
// If this function is concurrently called from two threads, only
// one thread will run the constructor, and the second will wait for
// it to complete (internally, gcc uses __cxa_guard_acquire to do that).
int f(){
    static mycounter a;
    return a.counter++;
}

int main(int ac, char** av)
{
    vector<thread> threads;
    constexpr int nthreads = 5;
    int results[nthreads];
    chrono::milliseconds::rep ms[nthreads];

    for (int i = 0; i < nthreads; i++) {
        threads.push_back(thread([i, &results, &ms] {
            auto start = chrono::high_resolution_clock::now();
            results[i] = f();
            auto end = chrono::high_resolution_clock::now();
            ms[i] = chrono::duration_cast<chrono::milliseconds>(end - start).
                    count();
        }));
    }

    for (auto &thread : threads) {
        thread.join();
    }

    report(constructions.load() == 1, "mycounter constructed once");

    bool seen[nthreads] {};
    for (int i =0; i < nthreads; i++) {
        auto r = results[i];
        report(r >= 0 && r < nthreads && !seen[i],
                string("thread ") + to_string(i));
        seen[i] = true;
        // We don't expect f to finish too quickly, it will need close to
        // a second to complete the initialization itself, or wait for
        // another thread doing the initalization.
        report(ms[i] > 500, string("thread ") + to_string(i) + " slow enough");
    }


    cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
