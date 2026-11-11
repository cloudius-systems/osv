/*
 * Copyright (C) 2026 ScyllaDB
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Test that a dlopen()-ed library (libtls_gd_user.so) can access, through a
// TLS descriptor, a thread-local variable defined in a library which was
// loaded with the application (libtls_gd.so) but has no static TLS block,
// because it accesses its thread-local variables only through
// __tls_get_addr(). The variable must then be accessed through the library's
// dynamic TLS block, not through static TLS that the threads don't have.

#include <iostream>
#include <thread>
#include <dlfcn.h>
#include <cassert>

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

extern "C" int gd1_get();

int main(int argc, char** argv)
{
    report(gd1_get() == 111, "gd1");
    auto handle = dlopen("/tests/libtls_gd_user.so", RTLD_NOW);
    assert(handle);
    auto gd1_increment = reinterpret_cast<void(*)()>(dlsym(handle, "gd1_increment"));
    assert(gd1_increment);
    gd1_increment();
    report(gd1_get() == 112, "gd1 modified");

    // In a new thread, gd1_increment() is the first to access gd1, so
    // the TLS block of libtls_gd.so is allocated by the TLS descriptor.
    std::thread t([gd1_increment] {
        gd1_increment();
        report(gd1_get() == 112, "gd1 modified in new thread");
    });
    t.join();
    report(gd1_get() == 112, "gd1 not modified by new thread");
    dlclose(handle);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
}
