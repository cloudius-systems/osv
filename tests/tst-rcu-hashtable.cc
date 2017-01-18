/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-rcu-hashtable

#include <boost/test/unit_test.hpp>
#include <osv/rcu-hashtable.hh>
#include <osv/mutex.h>
#include <osv/elf.hh>
#include <iostream>
#include <osv/printf.hh>
#include <osv/sched.hh>
#include <random>

struct test_element {
    test_element(unsigned val) : _val(val), _state(state::initialized) {
        ctors.fetch_add(1, std::memory_order_relaxed);
    }
    ~test_element() {
        dtors.fetch_add(1, std::memory_order_relaxed);
        _state = state::destroyed;
    }
    unsigned _val;
    enum class state { uninitialized, initialized, destroyed };
    state _state;
    bool operator==(const test_element& x) const { return _val == x._val; }
    void validate() const { assert(_state == state::initialized); }
    static std::atomic<long> ctors;
    static std::atomic<long> dtors;
    friend std::ostream& operator<<(std::ostream& os, const test_element& x) {
        return os << osv::sprintf("test_element{%s}", x._val);
    }
};

std::atomic<long> test_element::ctors;
std::atomic<long> test_element::dtors;

namespace std {

template <>
struct hash<test_element> {
    size_t operator()(const test_element& x) const { return x._val; }
};

}

BOOST_AUTO_TEST_CASE(test_rcu_hashtable_basic) {
    {
        osv::rcu_hashtable<test_element> ht;
        ht.emplace(5);
        bool r1, r2, r3;
        WITH_LOCK(osv::rcu_read_lock) {
            auto i1 = ht.reader_find(test_element(5));
            r1 = bool(i1);
            r2 = i1->_val == 5;
            auto i2  = ht.reader_find(test_element(6));
            r3 = !i2;
        }
        BOOST_REQUIRE(r1);
        BOOST_REQUIRE(r2);
        BOOST_REQUIRE(r3);
    }
    osv::rcu_flush();
    BOOST_REQUIRE(test_element::ctors == test_element::dtors);
}

struct element_status {
    int insertions_lower_bound = {};
    int insertions_upper_bound = {};
    int removals_lower_bound = {};
    int removals_upper_bound = {};
    friend std::ostream& operator<<(std::ostream& os, const element_status& es);
};

std::ostream& operator<<(std::ostream& os, const element_status& es)
{
    return os << osv::sprintf("element_status{ins_lb=%d ins_ub=%d rem_lb=%d rem_ub=%d}",
            es.insertions_lower_bound, es.insertions_upper_bound,
            es.removals_lower_bound, es.removals_upper_bound);
}


element_status* g_b, *g_a;
size_t g_i;
int* g_c;

void read_all(osv::rcu_hashtable<test_element>& ht,
        std::vector<element_status>& status)
{
    auto before = status;
    std::vector<int> count(status.size());
    WITH_LOCK(osv::rcu_read_lock) {
        ht.reader_for_each([&] (const test_element& e) {
            e.validate();
            ++count[e._val];
        });
    }
    auto after = status;
    g_b = before.data();
    g_a = after.data();
    g_c = count.data();
    for (size_t i = 0; i < status.size(); ++i) {
        g_i = i;
        assert(count[i] >= before[i].insertions_lower_bound - after[i].removals_upper_bound);
        assert(count[i] <= after[i].insertions_upper_bound - before[i].removals_lower_bound);
    }
}

void do_reads(osv::rcu_hashtable<test_element>& ht,
        size_t range,
        std::atomic<bool>& running,
        std::vector<element_status>& status)
{
    size_t counter = 0;
    while (running.load(std::memory_order_relaxed)) {
        std::default_random_engine generator;
        std::uniform_int_distribution<unsigned> gen_value(0, range - 1);
        auto value = gen_value(generator);
        if (counter++ % 100000 == 0) {
            read_all(ht, status);
        }
        WITH_LOCK(osv::rcu_read_lock) {
            element_status before = status[value];
            auto hash = [](size_t value) -> size_t { return value; };
            auto compare = [](unsigned x, const test_element& y) { return x == y._val; };
            auto i = ht.reader_find(value, hash, compare);
            element_status after = status[value];
            if (!i) {
                auto lower_bound = std::max(before.insertions_lower_bound - after.removals_upper_bound, 0);
                if (lower_bound > 0) {
                    DROP_LOCK(osv::rcu_read_lock) {
                        std::cerr << "before: " << before << "\nafter: " << after << "\n";
                    }
                }
                assert(lower_bound == 0);
            } else {
                i->validate();
                auto upper_bound = after.insertions_upper_bound - before.removals_lower_bound;
                if (upper_bound < 1) {
                    DROP_LOCK(osv::rcu_read_lock) {
                        std::cerr << "before: " << before << "\nafter: " << after << "\n";
                    }
                }
                assert(upper_bound >= 1);
                assert(*i == value);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(test_rcu_hashtable) {
    static const size_t range = 10000;
    static const size_t iterations = CONF_debug_memory ? 1000000 : 10000000;
    static const size_t nr_threads = 8;
    static const size_t max_size = CONF_debug_memory ? 50000 : 50000000;
    {
        {
            std::vector<element_status> status(range);
            osv::rcu_hashtable<test_element> ht;
            std::vector<std::unique_ptr<sched::thread>> reader_threads;
            std::atomic<bool> running = { true };
            for (size_t i = 0; i < nr_threads; ++i) {
                reader_threads.emplace_back(sched::thread::make([&] {
                    do_reads(ht, range, running, status);
                }));
            }
            for (auto& t : reader_threads) {
                t->start();
            }
            std::default_random_engine generator;
            std::uniform_int_distribution<unsigned> gen_action(0, 1);
            std::uniform_int_distribution<unsigned> gen_value(0, range - 1);
            size_t size = 0;
            for (size_t i = 0; i < iterations; ++i) {
                auto action = gen_action(generator);
                auto value = gen_value(generator);
                if (size > max_size) {
                    action = 1;
                }
                auto& s = status[value];
                switch (action) {
                case 0: { // insert
                    ++s.insertions_upper_bound;
                    ht.emplace(value);
                    ++s.insertions_lower_bound;
                    ++size;
                    break;
                }
                case 1: {// delete
                    auto ub = s.insertions_upper_bound - s.removals_lower_bound;
                    auto hash = [](size_t value) -> size_t { return value; };
                    auto compare = [](unsigned x, const test_element& y) { return x == y._val; };
                    auto i = ht.owner_find(value, hash, compare);
                    if (ub == 0) {
                        assert(!i);
                        BOOST_REQUIRE(!i);
                    } else {
                        ++s.removals_upper_bound;
                        ht.erase(i);
                        ++s.removals_lower_bound;
                        --size;
                    }
                    break;
                }
                default:
                    abort();
                }
            }
            running.store(false);
            for (auto& t : reader_threads) {
                t->join();
            }
        }
    osv::rcu_flush();
    BOOST_REQUIRE(test_element::ctors == test_element::dtors);
    }
}

OSV_ELF_MLOCK_OBJECT();
