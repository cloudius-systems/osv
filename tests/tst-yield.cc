/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/sched.hh>
#include "debug.hh"

int main(int argc, char **argv)
{
    debug("starting yield test\n");

    // Test that concurrent yield()s do not crash.
    constexpr int N = 10;
    constexpr int J = 10000000;
    sched::thread *ts[N];
    for (auto &t : ts) {
            t = new sched::thread([] {
                for (int j = 0; j < J; j++) {
                    sched::thread::yield();
                }
            });
            t->start();
    }
    for (auto t : ts) {
        t->join();
        delete t;
    }

    debug("yield test successful\n");
    return 0;
}
