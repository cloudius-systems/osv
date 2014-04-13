/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Tests for posix_memalign and aligned_alloc. Both should be able to handle
// various alignments and sizes, and free() should be able to free the
// memory allocated by them.
//
// To compile on Linux, use: g++ -g -pthread -std=c++11 tests/tst-align.cc


#include <string>
#include <iostream>
#include <vector>
#include <limits>
#include <algorithm>

#include <stdlib.h>
#include <malloc.h>

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

void test_posix_memalign(size_t alignment, size_t size)
{
    constexpr int tries = 100;
    std::vector<void*> ptrs;
    std::cout << "Testing posix_memalign alignment=" << alignment << ", size="
            << size << "\n";
    bool fail = false;
    size_t min_achieved = std::numeric_limits<size_t>::max();
    for (int i = 0; i < tries; i++) {
        void *ptr;
        fail = posix_memalign(&ptr, alignment, size) != 0;
        if (fail) {
            break;
        }
        ptrs.push_back(ptr);
        size_t achieved = 1 << __builtin_ctz((intptr_t)ptr);
        min_achieved = std::min(achieved, min_achieved);
    }
    for (auto ptr : ptrs) {
        free(ptr);
    }
    report(!fail, "posix_memalign " + std::to_string(alignment) + " " + std::to_string(size));
    report(min_achieved >= alignment, "achieved desired alignment");
    if (min_achieved > alignment) {
        // This case may be a waste, but it's actually fine according
        // to the standard, so just warn.
        std::cout << "WARNING: exceeded desired alignment - got " << min_achieved << "\n";
    }
}

int main(int ac, char** av)
{
    // posix_memalign expects its alignment argument to be a multiple of
    // sizeof(void*) (on 64 bit, that's 8 bytes) and a power of two.
    for (int shift = 3; shift <= 14; shift++) {
        size_t s = 1 << shift;
        test_posix_memalign(s, s);
        test_posix_memalign(s, s*2);
        test_posix_memalign(s, s*10);
//        test_posix_memalign(s, s/2); // doesn't work yet
//        test_posix_memalign(s, 8);  // doesn't work yet
    }

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
}


