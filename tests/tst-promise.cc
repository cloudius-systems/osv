/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-promise

#include <future>
#include <thread>
#include <chrono>

#include <boost/test/unit_test.hpp>

static auto one_sec = std::chrono::seconds(1);

BOOST_AUTO_TEST_CASE(test_set_value_wakes_waiting_thread)
{
    std::promise<bool> latch;
    std::promise<bool> consumer_done;

    std::thread consumer([&] {
        latch.get_future().wait();
        consumer_done.set_value(true);
    });
    consumer.detach();

    latch.set_value(true);
    BOOST_REQUIRE(consumer_done.get_future().wait_for(one_sec) == std::future_status::ready);
}
