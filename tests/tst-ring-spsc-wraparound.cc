/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/**
 * This test checks that ring_spsc push() continues to work after the internal
 * counter wraps around (issue #225)
 */

#include <lockfree/ring.hh>
#include <iostream>

using namespace std;

int main(int argc, char *argv[])
{
    ring_spsc<int, 256> test_ring;
    unsigned count;
    int val;

    for (count = 1; count != 0; count++) {
        if (!test_ring.push(1)) {
            cerr<<"FAIL to push at step "<<count<<endl;
            return 1;
        }

        if (!test_ring.pop(val)) {
            cerr<<"FAIL to pop at step "<<count<<endl;
            return 1;
        }
    }

    // The internal counter should wrap around now
    if (!test_ring.push(1)) {
        cerr<<"FAIL to push after wraparound"<<endl;
        return 1;
    }

    if (!test_ring.pop(val)) {
        cerr<<"FAIL to pop after wraparound"<<endl;
        return 1;
    }

    return 0;
}
