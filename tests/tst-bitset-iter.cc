/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-bitset-iter

#include <osv/bitset-iter.hh>
#include <boost/test/unit_test.hpp>
#include <functional>
#include <limits>

constexpr int ulong_bits = std::numeric_limits<unsigned long>::digits;
constexpr int long_bits = std::numeric_limits<long>::digits;

template<typename Range>
std::vector<int> make_vector(Range range)
{
    std::vector<int> v;
    std::copy(range.begin(), range.end(), std::back_inserter(v));
    return v;
}

void test_for_each_set(std::initializer_list<int> indexes, int offset = 0)
{
    std::bitset<32> bitset;

    for (int i : indexes) {
        bitset.set(i);
    }

    std::vector<int> expected;
    std::remove_copy_if(indexes.begin(), indexes.end(),
        std::back_inserter(expected),
        [=] (int x) -> bool {return x < offset; });

    auto actual = make_vector(bitsets::for_each_set(bitset, offset));
    BOOST_REQUIRE(actual == expected);
}

BOOST_AUTO_TEST_CASE(test_iteration_on_set_bits)
{
    test_for_each_set({});
    test_for_each_set({0});
    test_for_each_set({1});
    test_for_each_set({2});
    test_for_each_set({0, 2});
    test_for_each_set({0, 3});
    test_for_each_set({0, 1});
    test_for_each_set({0, 1, 2});
    test_for_each_set({0, 2, 3});
    test_for_each_set({1, 3, 4});
    test_for_each_set({3, 4, 5});
    test_for_each_set({3, 4, 6, 7});
    test_for_each_set({3, 8});
    test_for_each_set({1, 2, 3, 4, 5});

    test_for_each_set({}, 2);
    test_for_each_set({0}, 2);
    test_for_each_set({1}, 2);
    test_for_each_set({2}, 2);
    test_for_each_set({3}, 2);
    test_for_each_set({4}, 2);
    test_for_each_set({1, 3}, 2);
    test_for_each_set({1, 4}, 2);
    test_for_each_set({1, 4, 5}, 2);
}

BOOST_AUTO_TEST_CASE(test_get_last_set)
{
    std::bitset<64> bitset;

    for (int i = 0; i < 64; i++) {
        bitset.set(i);
        BOOST_REQUIRE_EQUAL(bitsets::get_last_set(bitset), i);
    }
}

BOOST_AUTO_TEST_CASE(test_get_first_set)
{
    std::bitset<64> bitset;

    for (int i = 63; i >= 0; i--) {
        bitset.set(i);
        BOOST_REQUIRE_EQUAL(bitsets::get_first_set(bitset), i);
    }
}

BOOST_AUTO_TEST_CASE(test_get_last_set_on_small_bitset)
{
    std::bitset<4> bitset;

    bitset.set(0);
    BOOST_REQUIRE_EQUAL(bitsets::get_last_set(bitset), 0);

    bitset.set(1);
    BOOST_REQUIRE_EQUAL(bitsets::get_last_set(bitset), 1);

    bitset.set(3);
    BOOST_REQUIRE_EQUAL(bitsets::get_last_set(bitset), 3);
}

BOOST_AUTO_TEST_CASE(test_count_leading_zeros_on_ulong)
{
    for (int i = 0; i < ulong_bits; i++) {
        BOOST_REQUIRE_EQUAL(ulong_bits - i - 1, bitsets::count_leading_zeros(1UL << i));
    }

    BOOST_REQUIRE_EQUAL(0, bitsets::count_leading_zeros(std::numeric_limits<unsigned long>::max()));
}

BOOST_AUTO_TEST_CASE(test_count_leading_zeros_on_long)
{
    for (int i = 0; i < long_bits; i++) {
        BOOST_REQUIRE_EQUAL(long_bits - i - 1, bitsets::count_leading_zeros(1L << i));
    }

    BOOST_REQUIRE_EQUAL(0, bitsets::count_leading_zeros(std::numeric_limits<long>::max()));
}

BOOST_AUTO_TEST_CASE(test_count_trailing_zeros_on_ulong)
{
    for (int i = 0; i < ulong_bits; i++) {
        BOOST_REQUIRE_EQUAL(i, bitsets::count_trailing_zeros(1UL << i));
    }

    BOOST_REQUIRE_EQUAL(0, bitsets::count_trailing_zeros(0x1UL));
    BOOST_REQUIRE_EQUAL(1, bitsets::count_trailing_zeros(0x2UL));
    BOOST_REQUIRE_EQUAL(0, bitsets::count_trailing_zeros(0x3UL));
    BOOST_REQUIRE_EQUAL(2, bitsets::count_trailing_zeros(0x4UL));
}

BOOST_AUTO_TEST_CASE(test_count_trailing_zeros_on_long)
{
    for (int i = 0; i < long_bits; i++) {
        BOOST_REQUIRE_EQUAL(i, bitsets::count_trailing_zeros(1L << i));
    }

    BOOST_REQUIRE_EQUAL(0, bitsets::count_trailing_zeros(0x1L));
    BOOST_REQUIRE_EQUAL(1, bitsets::count_trailing_zeros(0x2L));
    BOOST_REQUIRE_EQUAL(0, bitsets::count_trailing_zeros(0x3L));
    BOOST_REQUIRE_EQUAL(2, bitsets::count_trailing_zeros(0x4L));
}
