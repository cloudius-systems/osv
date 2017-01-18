/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#define BOOST_TEST_MODULE tst-wait-for

#include <boost/test/unit_test.hpp>
#include <osv/elf.hh>
#include <osv/sched.hh>
#include <osv/waitqueue.hh>
#include <osv/clock.hh>
#include <cstdlib>

using namespace osv::clock::literals;

BOOST_AUTO_TEST_CASE(test_wait_for_one_timer)
{
    auto now = osv::clock::uptime::now();
    sched::timer tmr(*sched::thread::current());
    tmr.set(now + 1_s);
    sched::thread::wait_for(tmr);
    auto later = osv::clock::uptime::now();
    BOOST_REQUIRE(std::abs(later - (now + 1_s)) < 20_ms);
    BOOST_REQUIRE(tmr.expired());
}

BOOST_AUTO_TEST_CASE(test_wait_for_two_timers)
{
    auto now = osv::clock::uptime::now();
    sched::timer tmr1(*sched::thread::current());
    sched::timer tmr2(*sched::thread::current());
    tmr1.set(now + 2_s);
    tmr2.set(now + 1_s);
    sched::thread::wait_for(tmr1, tmr2);
    BOOST_REQUIRE(!tmr1.expired() && tmr2.expired());
    tmr2.cancel();
    sched::thread::wait_for(tmr1, tmr2);
    BOOST_REQUIRE(tmr1.expired() && !tmr2.expired());
}

BOOST_AUTO_TEST_CASE(test_waitqueue_1)
{
    waitqueue wq;
    mutex mtx;
    int counter = 0;
    WITH_LOCK(mtx) {
        std::unique_ptr<sched::thread> waker(sched::thread::make([&] {
            WITH_LOCK(mtx) {
                ++counter;
                wq.wake_one(mtx);
            }
        }));
        waker->start();
        wq.wait(mtx);
        waker->join();
    }
    BOOST_REQUIRE(counter == 1);
}

BOOST_AUTO_TEST_CASE(test_waitqueue_2)
{
    waitqueue wq;
    mutex mtx;
    int counter = 0;
    sched::timer tmr(*sched::thread::current());
    WITH_LOCK(mtx) {
        tmr.set(500_ms);
        std::unique_ptr<sched::thread> waker(sched::thread::make([&] {
            sched::thread::sleep(1_s);
            WITH_LOCK(mtx) {
                ++counter;
                wq.wake_one(mtx);
            }
        }));
        waker->start();
        // timer wait
        sched::thread::wait_for(mtx, wq, tmr);
        BOOST_REQUIRE(tmr.expired());
        BOOST_REQUIRE(counter == 0);
        // null wait
        sched::thread::wait_for(mtx, wq, tmr);
        BOOST_REQUIRE(tmr.expired());
        BOOST_REQUIRE(counter == 0);
        // wait for wq
        tmr.cancel();
        sched::thread::wait_for(mtx, wq, tmr);
        BOOST_REQUIRE(counter == 1);
        waker->join();
    }
}

BOOST_AUTO_TEST_CASE(test_wait_for_predicate)
{
    std::atomic<bool> x = { false };
    auto sleeper = sched::thread::current();
    std::unique_ptr<sched::thread> waker(sched::thread::make([&] {
        sched::thread::sleep(1_s);
        x.store(true);
        sleeper->wake();
    }));
    waker->start();
    // send some spurious wakeups for fun
    std::unique_ptr<sched::thread> false_waker(sched::thread::make([&] {
        for (auto i = 0; i < 100; ++i) {
            sched::thread::sleep(100_ms);
            sleeper->wake();
        }
    }));
    sched::timer tmr(*sched::thread::current());
    tmr.set(500_ms);
    sched::thread::wait_for(tmr, [&] { return x.load(); });
    BOOST_REQUIRE(tmr.expired());
    BOOST_REQUIRE(!x.load());
    tmr.cancel();
    sched::thread::wait_for(tmr, [&] { return x.load(); });
    BOOST_REQUIRE(!tmr.expired());
    BOOST_REQUIRE(x.load());
    waker->join();
    false_waker->join();
}

OSV_ELF_MLOCK_OBJECT();
