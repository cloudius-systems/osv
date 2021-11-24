/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <thread>
#include <cstdio>

int main(int argc, char **argv)
{
    printf("starting yield test\n");

    // Test that concurrent yield()s do not crash.
    constexpr int N = 10;
    constexpr int J = 10000000;
    std::thread *ts[N];
    for (auto &t : ts) {
            t = new std::thread([] {
                for (int j = 0; j < J; j++) {
                    std::this_thread::yield();
                }
            });
    }
    for (auto t : ts) {
        t->join();
        delete t;
    }

    printf("yield test successful\n");
    return 0;
}
