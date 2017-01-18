/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/run.hh>
#include <atomic>
#include <iostream>

static std::atomic_int tests {0}, fails {0};

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    debug("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

extern "C" long gettid();


int main(int ac, char** av)
{
    bool child = (ac == 2) && (av[1][0] == '\0');
    std::cout << "main: thread id=" << gettid() << ", child=" << child << "\n";

    if (child) {
        return gettid();
    }

    // See that I can run myself (with a special argument to stop the recursion)
    const char *child_args[] = {"/tests/tst-run.so", ""};
    int ret;
    bool b = (bool)osv::run("/tests/tst-run.so", 2, child_args, &ret);
    report(b == true, "Run myself");
    // Check that the child ran in my thread - not a new thread
    report(ret == gettid(), "Child ran in same thread as parent");


    // Check that running a non-existant object fails
    try {
        osv::run("/nonexistant.so", 0, nullptr, nullptr);
        // should throw and not get here
        report(false, "Run nonexistant");
    } catch(osv::launch_error) {
        report(true, "Run nonexistant");
    }

    return 0;
}


