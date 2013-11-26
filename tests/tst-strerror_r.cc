/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// strerror_r() changes its behavior depending on the source
// dialect, test to make sure we get the right one.

#include <boost/system/error_code.hpp>
#include <string>
#include <iostream>

using namespace boost::system;
using namespace std;

unsigned tests, failures;

void report(bool ok, string msg)
{
    ++tests;
    failures += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

int main(int ac, char** av)
{
    error_code ec(EPERM, system_category());
    report(ec.message().substr(7) != "unknown", "strerror_r() called from a _GNU_SOURCE binary");
    std::cout << "Test complete (" << failures << "/" << tests << " failures)\n";
    return failures ? 1 : 0;
}
