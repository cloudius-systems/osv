/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Benchmark the speed overhead of using a thread-local variables in an
// application. The normal overhead of a thread-local variable in a Linux
// application is very small, because the application is compiled as an
// executable, the "initial exec" TLS model is used, where the variable's
// address is computed with compile-time-generated code. The overhead in
// OSv applications, compiled as shared libraries, are higher because it
// uses the "global dynamic" TLS model and every TLS access becomes a call
// to the __tls_get_addr function - but here we want to test just how slow
// it is.

#include <chrono>
#include <iostream>
#include <dlfcn.h>
#include <cassert>

__thread int var_tls;
int var_global;

int main()
{
    constexpr int N = 100000000;
    auto start = std::chrono::system_clock::now();
    for (register int i = 0; i < N; i++) {
        // To force gcc to not optimize this loop away
        asm volatile("" : : : "memory");
        ++var_global;
    }
    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> sec = end - start;
    std::cout << "var_global iteration (ns): " << (sec.count() / N / 1e-9) << "\n";

    start = std::chrono::system_clock::now();
    for (register int i = 0; i < N; i++) {
        // To force gcc to not optimize this loop away
        asm volatile("" : : : "memory");
        ++var_tls;
    }
    end = std::chrono::system_clock::now();
    sec = end - start;
    std::cout << "var_tls iteration (ns): " << (sec.count() / N / 1e-9) << "\n";

    auto handle = dlopen("/tests/lib-misc-tls.so", RTLD_NOW);
    assert(handle);
    void (*external_library)(int) = reinterpret_cast<void(*)(int)>(dlsym(handle, "external_library"));
    assert(external_library);

    start = std::chrono::system_clock::now();
    external_library(N);
    end = std::chrono::system_clock::now();
    sec = end - start;
    std::cout << "var_lib_tls iteration (ns): " << (sec.count() / N / 1e-9) << "\n";

    dlclose(handle);
    return 0;
}
