/*
 * Copyright (C) 2017 ScyllaDB, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This test works on both Linux and OSv.
// To compile on Linux, use: c++ -std=c++11 tests/tst-math.cc

#include <math.h>

#include <iostream>

static int tests = 0, fails = 0;

#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals <<
                ", expected " << expecteds << ", saw " << actual << ".\n";
        return false;
    }
    return true;
}

#define expect_errno(call, experrno) ( \
        do_expect(call, -1, #call, "-1", __FILE__, __LINE__) && \
        do_expect(errno, experrno, #call " errno",  #experrno, __FILE__, __LINE__) )

int main(int argc, char **argv)
{
    // Test nearbyint()
    expect(nearbyint(1.3), 1.0);
    expect(nearbyint(1.7), 2.0);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
