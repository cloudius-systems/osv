/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * To compile on Linux:
 * g++ -g -pthread -std=c++11 tests/tst-timer-set.cc -o tests/tst-timer-set \
 *   -I./include -lboost_unit_test_framework -DBOOST_TEST_DYN_LINK
 */

#define BOOST_TEST_MODULE tst-timer-set

#include <chrono>
#include <unordered_set>
#include <stdio.h>
#include <osv/timer-set.hh>
#include <boost/test/unit_test.hpp>

using Clock = std::chrono::steady_clock;

class test_timer
{
private:
    Clock::time_point _timeout;

public:
    test_timer(Clock::time_point _time_point)
        : _timeout(_time_point)
    {
    }

    Clock::time_point get_timeout()
    {
        return _timeout;
    }

    void set_timeout(Clock::time_point new_timeout)
    {
        _timeout = new_timeout;
    }
public:
    bi::list_member_hook<> link;
};

using timer_set_t = timer_set<test_timer, &test_timer::link, Clock>;
using timer_ptr_set = std::unordered_set<test_timer*>;

static Clock::time_point next_time_point()
{
    static Clock::time_point next = Clock::now();
    auto ret = next;
    next += Clock::duration(1);
    return ret;
}

static Clock::time_point abs_time_point(Clock::duration::rep value)
{
    return Clock::time_point(Clock::duration(value));
}

static timer_ptr_set get_expired(timer_set_t& timers)
{
    timer_ptr_set set;
    test_timer* timer;
    while ((timer = timers.pop_expired())) {
        set.insert(timer);
    }
    return set;
}

BOOST_AUTO_TEST_CASE(test_typical_timer_insertion_and_expiry)
{
    timer_set_t _timers;

    // consecutive moments
    Clock::time_point m1 = next_time_point();
    Clock::time_point m2 = next_time_point();
    Clock::time_point m3 = next_time_point();
    Clock::time_point m4 = next_time_point();
    Clock::time_point m5 = next_time_point();
    Clock::time_point m6 = next_time_point();

    test_timer t1(m1);
    test_timer t2(m2);
    test_timer t3(m4);

    BOOST_TEST_MESSAGE("Expire when no timers inserted yet");
    _timers.expire(m3);
    BOOST_REQUIRE(_timers.pop_expired() == nullptr);
    BOOST_REQUIRE(_timers.get_next_timeout() == Clock::time_point::max());

    BOOST_REQUIRE_EQUAL(_timers.insert(t2), true);
    BOOST_REQUIRE_EQUAL(_timers.insert(t3), false);
    BOOST_REQUIRE_EQUAL(_timers.insert(t1), true);

    BOOST_TEST_MESSAGE("Expiring now should yield the first two timers");
    _timers.expire(m3);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t1, &t2}));
    BOOST_REQUIRE(_timers.get_next_timeout() == m4);

    BOOST_TEST_MESSAGE("Expiring at the same moment again yields no timers");
    _timers.expire(m3);
    BOOST_REQUIRE(_timers.pop_expired() == nullptr);
    BOOST_REQUIRE(_timers.get_next_timeout() == m4);

    test_timer t4(m4);
    test_timer t5(m5);

    BOOST_REQUIRE_EQUAL(_timers.insert(t5), false);
    BOOST_REQUIRE_EQUAL(_timers.insert(t4), false);

    BOOST_TEST_MESSAGE("Expire two timers at the same time point");
    _timers.expire(m4);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t3, &t4}));
    BOOST_REQUIRE(_timers.get_next_timeout() == m5);

    BOOST_TEST_MESSAGE("Expire last timer");
    _timers.expire(m6);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t5}));
    BOOST_REQUIRE(_timers.get_next_timeout() == Clock::time_point::max());
}

BOOST_AUTO_TEST_CASE(test_next_timeout_is_updated_correctly_after_expiry_when_there_are_timers_in_higher_buckets)
{
    timer_set_t _timers;

    // each moment in different bucket
    Clock::time_point m1 = abs_time_point(1 << 1);
    Clock::time_point m2 = abs_time_point(1 << 2);
    Clock::time_point m3 = abs_time_point(1 << 3);

    test_timer t1(m1);
    test_timer t2(m2);
    test_timer t3(m3);

    _timers.insert(t1);
    _timers.insert(t3);

    BOOST_TEST_MESSAGE("Expiring in a way that there should be still active timers in higher buckets");
    _timers.expire(m1);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t1}));
    BOOST_REQUIRE(_timers.get_next_timeout() == m3);

    BOOST_TEST_MESSAGE("Checking that it is signalled that timer 2 is now the earliest timer");
    BOOST_REQUIRE_EQUAL(_timers.insert(t2), true);

    _timers.clear();
}

BOOST_AUTO_TEST_CASE(test_edge_case_expiry)
{
    timer_set_t _timers;

    Clock::time_point m1 = next_time_point();
    Clock::time_point m_max = Clock::time_point::max();

    test_timer t1(m1);
    test_timer t2(m_max);

    _timers.insert(t1);
    _timers.insert(t2);

    _timers.expire(m1);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t1}));
    BOOST_REQUIRE(_timers.get_next_timeout() == m_max);

    _timers.expire(m_max);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t2}));
    BOOST_REQUIRE(_timers.get_next_timeout() == m_max);

    _timers.expire(m_max);
    BOOST_REQUIRE(_timers.pop_expired() == nullptr);
    BOOST_REQUIRE(_timers.get_next_timeout() == m_max);
}

BOOST_AUTO_TEST_CASE(test_removal)
{
    timer_set_t _timers;

    Clock::time_point m1 = abs_time_point(1 << 1);
    Clock::time_point m2 = abs_time_point(1 << 2);
    Clock::time_point m3 = abs_time_point(1 << 3);
    Clock::time_point m5 = abs_time_point(1 << 5);

    test_timer t1(m2);
    test_timer t2(m3);
    test_timer t3(m5);

    _timers.insert(t1);
    _timers.insert(t2);
    _timers.insert(t3);

    _timers.remove(t2);
    t2.set_timeout(m1);
    BOOST_REQUIRE_EQUAL(_timers.insert(t2), true);

    _timers.expire(m1);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t2}));

    _timers.remove(t3);
    _timers.remove(t1);
    _timers.expire(m5);
    BOOST_REQUIRE(_timers.pop_expired() == nullptr);
    BOOST_REQUIRE(_timers.get_next_timeout() == Clock::time_point::max());
}

BOOST_AUTO_TEST_CASE(test_expiry_when_some_timers_remain_in_the_expired_bucket)
{
    timer_set_t _timers;

    long bit_4 = 8;
    long bit_5 = 16;
    long bit_6 = 32;
    long bit_7 = 64;
    Clock::time_point m1 = abs_time_point(bit_5);
    Clock::time_point m2 = abs_time_point(bit_5 + bit_4 + 1);
    Clock::time_point m3 = abs_time_point(bit_5 + bit_4 + 2);
    Clock::time_point m4 = abs_time_point(bit_5 + bit_4 + 3);
    Clock::time_point m5 = abs_time_point(bit_5 + bit_4 + 4);
    Clock::time_point m6 = abs_time_point(bit_5 + bit_4 + 5);
    Clock::time_point m7 = abs_time_point(bit_6);
    Clock::time_point m8 = abs_time_point(bit_7);

    _timers.expire(m1);

    test_timer t1(m2);
    test_timer t2(m3);
    test_timer t3(m5);
    test_timer t4(m6);
    test_timer t5(m8);

    _timers.insert(t1);
    _timers.insert(t2);
    _timers.insert(t3);
    _timers.insert(t4);
    _timers.insert(t5);

    BOOST_TEST_MESSAGE("timers t1-t6 share a bucket, expire only some");
    _timers.expire(m4);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t1, &t2}));
    BOOST_REQUIRE(_timers.get_next_timeout() == m5);

    BOOST_TEST_MESSAGE("now check if the remaining timers will expire when we expire after their bucket");
    _timers.expire(m7);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t3, &t4}));
    BOOST_REQUIRE(_timers.get_next_timeout() == m8);

    _timers.expire(Clock::time_point::max());
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t5}));
}

BOOST_AUTO_TEST_CASE(test_empty_method)
{
    timer_set_t _timers;

    BOOST_REQUIRE(_timers.empty());

    test_timer t1(next_time_point());
    test_timer t2(next_time_point());

    _timers.insert(t1);
    BOOST_REQUIRE(!_timers.empty());

    _timers.insert(t2);
    BOOST_REQUIRE(!_timers.empty());

    _timers.remove(t1);
    BOOST_REQUIRE(!_timers.empty());

    _timers.remove(t2);
    BOOST_REQUIRE(_timers.empty());
}

BOOST_AUTO_TEST_CASE(test_handling_of_timers_which_are_already_expired_at_insertion)
{
    timer_set_t _timers;

    // consecutive moments
    Clock::time_point m1 = next_time_point();
    Clock::time_point m2 = next_time_point();
    Clock::time_point m3 = next_time_point();

    _timers.expire(m2);

    test_timer t1(m1);
    test_timer t2(m2);
    test_timer t3(m3);

    BOOST_REQUIRE_EQUAL(_timers.insert(t2), true);
    BOOST_REQUIRE(_timers.get_next_timeout() == m2);

    BOOST_REQUIRE_EQUAL(_timers.insert(t1), true);
    BOOST_REQUIRE(_timers.get_next_timeout() == m2);

    BOOST_REQUIRE_EQUAL(_timers.insert(t3), false);
    BOOST_REQUIRE(_timers.get_next_timeout() == m2);

    BOOST_REQUIRE(!_timers.empty());

    _timers.expire(m2);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t1, &t2}));
    BOOST_REQUIRE(_timers.get_next_timeout() == m3);
    BOOST_REQUIRE(!_timers.empty());

    _timers.expire(m2);
    BOOST_REQUIRE(_timers.pop_expired() == nullptr);
    BOOST_REQUIRE(_timers.get_next_timeout() == m3);
    BOOST_REQUIRE(!_timers.empty());

    BOOST_REQUIRE_EQUAL(_timers.insert(t1), true);
    BOOST_REQUIRE(_timers.get_next_timeout() == m2);

    _timers.expire(m2);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t1}));
    BOOST_REQUIRE(_timers.get_next_timeout() == m3);

    _timers.expire(m3);
    BOOST_REQUIRE(get_expired(_timers) == timer_ptr_set({&t3}));
    BOOST_REQUIRE(_timers.empty());
}
