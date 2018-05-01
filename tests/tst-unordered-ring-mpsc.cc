/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-unordered-ring-mpsc

#include <boost/test/unit_test.hpp>

#include <lockfree/unordered_ring_mpsc.hh>
#include <thread>
#include <iostream>
#include <unordered_set>
#include <osv/latch.hh>
#include <osv/elf.hh>
#include <osv/migration-lock.hh>

using value_t = int;
using set_t = std::unordered_set<value_t>;

template<typename Ring>
static set_t drain(Ring& ring)
{
    set_t set;
    for (auto elem : ring.drain())
    {
        set.insert(elem);
    }
    return set;
}

BOOST_AUTO_TEST_CASE(test_insert_and_drain)
{
    SCOPE_LOCK(migration_lock);
    unordered_ring_mpsc<value_t,16> ring;

    assert(ring.push(1));
    assert(ring.push(2));
    assert(ring.push(3));
    assert(ring.push(4));
    assert(ring.push(5));

    assert(drain(ring) == set_t({1, 2, 3, 4, 5}));
    assert(drain(ring) == set_t({}));

    assert(ring.push(6));
    assert(ring.push(7));

    assert(drain(ring) == set_t({6, 7}));
}

BOOST_AUTO_TEST_CASE(test_when_ring_gets_full)
{
    SCOPE_LOCK(migration_lock);
    unordered_ring_mpsc<value_t,4> ring;

    assert(ring.push(1));
    assert(ring.push(2));
    assert(ring.push(3));
    assert(ring.push(4));
    assert(!ring.push(5));
    assert(!ring.push(6));

    assert(drain(ring) == set_t({1, 2, 3, 4}));

    assert(ring.push(5));
    assert(ring.push(6));
    assert(ring.push(7));
    assert(ring.push(8));

    assert(drain(ring) == set_t({5, 6, 7, 8}));
}

BOOST_AUTO_TEST_CASE(test_concurrent_access)
{
    SCOPE_LOCK(migration_lock);
    constexpr value_t n_items = 1024*128;
    unordered_ring_mpsc<value_t,n_items*2> ring;

    latch all_lined_up(2);
    latch done(2);
    latch proceed;

    std::thread t1([&] {
        all_lined_up.count_down();
        proceed.await();

        for (value_t i = 0; i < n_items; i++) {
            ring.push(i);
        }

        done.count_down();
    });

    std::thread t2([&] {
        all_lined_up.count_down();
        proceed.await();

        for (value_t i = n_items; i < n_items*2; i++) {
            ring.push(i);
        }

        done.count_down();
    });

    all_lined_up.await();
    proceed.count_down();

    set_t all;

    while (!done.is_released()) {
        for (auto e : ring.drain()) {
            all.insert(e);
        }
    }

    for (auto e : ring.drain()) {
        all.insert(e);
    }

    for (value_t i = 0; i < n_items*2; i++) {
        assert(all.find(i) != all.end());
    }

    t1.join();
    t2.join();
}

OSV_ELF_MLOCK_OBJECT();
