/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// test the fpu, especially with preemption enabled

#include <osv/sched.hh>
#include <vector>
#include <atomic>
#include <osv/debug.hh>
#include <boost/format.hpp>
#include <cmath>
#include <fenv.h>

bool test()
{
    constexpr unsigned nr_angles = 100, repeats = 100000;
    double sins[nr_angles];
    bool bad = false;
    for (unsigned i = 0; i < nr_angles; ++i) {
        sins[i] = std::sin(double(i));
    }
    for (unsigned j = 0; j < repeats; ++j) {
        for (unsigned i = 0; i < nr_angles; ++i) {
            double v1 = std::sin(double(i));
            double v2 = sins[i];
            bad |= v1 != v2;
            if (bad) {
                while (true) ;
            }
        }
    }
    debug(boost::format("3 -> %f\n") % sins[3]);
    return !bad;
}

int callee_saved_loop(int mode, bool yield)
{
    constexpr unsigned repeats = 1000000;
    fesetround(mode);
    for (unsigned i = 0; i < repeats; ++i) {
        int rmode = fegetround();
        if (rmode != mode) {
            printf("thread expected %d, got %d (yield = %d)\n", mode, rmode, yield);
            return 1;
        }
        fesetround(mode);
        if (yield) {
            sched::thread::yield();
        }
    }
    return 0;
}

int callee_saved_zero(bool yield)
{
    return callee_saved_loop(FE_TOWARDZERO, yield);
}

int callee_saved_nearest(bool yield)
{
    return callee_saved_loop(FE_TONEAREST, yield);
}

typedef boost::format fmt;

int main(int ac, char **av)
{
    constexpr unsigned nr_threads = 16;
    std::vector<sched::thread*> threads;

    debug("starting fpu test\n");
    std::atomic<int> tests{}, fails{};
    for (unsigned i = 0; i < nr_threads; ++i) {
        auto t = sched::thread::make([&] {
            if (!test()) {
                ++fails;
            }
            ++tests;
        });
        threads.push_back(t);
        t->start();
    }
    for (auto t : threads) {
        t->join();
    }
    threads.clear();

    auto do_callee_saved = [&](bool yield) {
        tests++;
        for (unsigned i = 0; i < nr_threads; ++i) {
            tests++;
            sched::thread *t;
            if (i % 2) {
                t = sched::thread::make([&] { fails += callee_saved_zero(yield); }, sched::thread::attr().pin(sched::cpus[0]));
            } else {
                t = sched::thread::make([&] { fails += callee_saved_nearest(yield); }, sched::thread::attr().pin(sched::cpus[0]));
            }
            threads.push_back(t);
            t->start();
        }

        for (auto t : threads) {
            t->join();
        }
        threads.clear();
    };

    do_callee_saved(false);
    do_callee_saved(true);

    printf("fpu test done, %d/%d fails/tests\n", fails.load(std::memory_order_relaxed), tests.load(std::memory_order_relaxed));
    return !fails ? 0 : 1;
}
