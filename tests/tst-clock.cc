/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <osv/clock.hh>

#define BOOST_TEST_MODULE tst-clock

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE(test_uptime_clock_monotonic) {
    using clock = osv::clock::uptime;

    auto start = clock::now();
    auto end = start + std::chrono::seconds(15);
    auto prev = start;
    clock::time_point now;
    do {
        now = clock::now();
        BOOST_REQUIRE(now >= prev);
        prev = now;
    } while (now < end);
}
