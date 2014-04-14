/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#define BOOST_TEST_MODULE tst-rcu-list

#include <boost/test/unit_test.hpp>

#include <osv/rcu-list.hh>
#include <atomic>
#include <cassert>
#include <vector>
#include <osv/sched.hh>
#include <osv/semaphore.hh>
#include <random>
#include <osv/elf.hh>
#include <osv/debug.hh>

struct test_element {
    static constexpr int magic = 0x12345678;
    test_element(int v) : v(v), vv(v ^ magic) { ctors.fetch_add(1, std::memory_order_relaxed); }
    test_element(const test_element& e) : v(e.v), vv(e.vv) { verify(); ctors.fetch_add(1, std::memory_order_relaxed); }
    ~test_element() { verify(); v = vv = 0; dtors.fetch_add(1, std::memory_order_relaxed); }
    void verify() { if (vv != (v ^ magic)) { miscompares.fetch_add(1, std::memory_order_relaxed); } }
    int v;
    int vv;
    static std::atomic<unsigned long> ctors;
    static std::atomic<unsigned long> dtors;
    static std::atomic<unsigned long> miscompares;
};

std::atomic<unsigned long> test_element::ctors;
std::atomic<unsigned long> test_element::dtors;
std::atomic<unsigned long> test_element::miscompares;

void do_reads(osv::rcu_list<test_element>& list, std::atomic<bool>& running, semaphore& sem)
{
    std::default_random_engine generator;
    std::uniform_int_distribution<unsigned> distribution(0, 50);
    while (running.load(std::memory_order_relaxed)) {
        WITH_LOCK(osv::rcu_read_lock) {
            auto rlist = list.for_read();
            auto n = distribution(generator);
            for (auto& e : rlist) {
                e.verify();
                if (n-- == 0) {
                    break;
                }
            }
        }
    }
    sem.post();
}

void do_writes(osv::rcu_list<test_element>& list, unsigned iterations)
{
    std::default_random_engine generator;
    std::uniform_int_distribution<unsigned> gen_action(0, 1);
    std::uniform_int_distribution<unsigned> gen_delete_pos(0, 9);
    for (unsigned i = 0; i < iterations; ++i) {
        auto action = gen_action(generator);
        switch (action) {
        case 0: // insert
            list.by_owner().emplace_front(generator());
            break;
        case 1: { // delete
            auto mlist = list.by_owner();
            auto pos = gen_delete_pos(generator);
            for (auto i = mlist.begin(); i != mlist.end(); ++i) {
                if (pos--) {
                    continue;
                }
                mlist.erase(i);
                break;
            }
            break;
        }
        default:
            abort();
        }
    }
}

BOOST_AUTO_TEST_CASE(test_rcu_list) {
    {
        std::atomic<bool> running = { true };
        semaphore sem(0);
        osv::rcu_list<test_element> list;
        std::vector<std::unique_ptr<sched::thread>> readers;
        for (int i = 0; i < 20; ++i) {
            readers.emplace_back(new sched::thread([&] { do_reads(list, running, sem); }));
            readers.back()->start();
        }
        do_writes(list, 1000000);
        running.store(false, std::memory_order_relaxed);
        sem.wait(readers.size());
        for (auto& r : readers) {
            r->join();
        }
    }
    // force test_element deferred destructors to run
    osv::rcu_flush();
    debug("ctors:        %10d\n", test_element::ctors.load());
    debug("dtors:        %10d\n", test_element::dtors.load());
    debug("miscompares:  %10d\n", test_element::miscompares.load());
    BOOST_REQUIRE(test_element::miscompares.load(std::memory_order_relaxed) == 0);
    BOOST_REQUIRE(test_element::ctors.load(std::memory_order_relaxed) == test_element::dtors.load(std::memory_order_relaxed));
}

OSV_ELF_MLOCK_OBJECT();
