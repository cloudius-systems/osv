/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// To compile on Linux, use:
//     g++ -g -fPIC -shared -std=c++11 libtls.cc -o libtls.so
//     g++ -g -pthread -std=c++11 tst-tls.cc libtls.so -Wl,-R.

#include <iostream>
#include <thread>
#include <dlfcn.h>
#include <cassert>

static int tests = 0, fails = 0;

// A thread-local variable used in a shared object and visible to other shared
// objects uses the "General Dynamic" TLS model (sometimes known as Global
// Dynamic).
__thread int v1 = 123;
__thread int v2 = 234;

#ifndef __DLOPEN__
extern __thread int ex1;
extern __thread int ex2;
#endif

// If the compiler knows the thread-local variable must be defined in the same
// shared object, it can use the "Local Dynamic" TLS model.
static __thread int v3 = 345;
static __thread int v4 = 456;

// The compiler and linker won't normally use the "Initial Exec" TLS model in
// a shared library, but some libraries are compiled with
// -ftls-model=initial-exec, and we can force this model on one variable
// with an attribute.
__thread int v5 __attribute__ ((tls_model ("initial-exec"))) = 567;
static __thread int v6 __attribute__ ((tls_model ("initial-exec"))) = 678;

#ifndef __DLOPEN__
extern __thread int ex3 __attribute__ ((tls_model ("initial-exec")));
#endif

#ifndef __SHARED_OBJECT__
// We can also try to force the "Local Exec" TLS model, but OSv's makefile
// builds all tests as shared objects (.so), and the linker will report an
// error, because local-exec is not allowed in shared libraries, just in
// executables (including PIE).
__thread long v7 __attribute__ ((tls_model ("local-exec"))) = 987UL;
__thread int v8 __attribute__ ((tls_model ("local-exec"))) = 789;
__thread int v9 __attribute__ ((tls_model ("local-exec")));
__thread int v10 __attribute__ ((tls_model ("local-exec"))) = 1111;
#endif

#ifndef __DLOPEN__
extern "C" void external_library();
#endif

static void report(bool ok, std::string msg, bool use_printf = false)
{
    ++tests;
    fails += !ok;
    if (use_printf) {
        printf("%s: %s\n", ok ? "PASS" : "FAIL", msg.c_str());
    } else {
        std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
    }
}

int main(int argc, char** argv)
{
    report(v1 == 123, "v1");
    report(v2 == 234, "v2");
#ifdef __DLOPEN__
    //We do not test ex2 variable because in libtls.cc it is set
    //to be accessed via initial-exec which by design is not possible
    //if libtls.so is dlopen-ed
    auto handle = dlopen("/tests/libtls.so", RTLD_NOW);
    assert(handle);
    int *ex1_ptr = reinterpret_cast<int*>(dlsym(handle, "ex1"));
    int *ex3_ptr = reinterpret_cast<int*>(dlsym(handle, "ex3"));
    assert(ex1_ptr && ex3_ptr);
    report(*ex1_ptr == 321, "ex1");
    report(*ex3_ptr == 765, "ex3");
#else
    report(ex1 == 321, "ex1");
    report(ex2 == 432, "ex2");
    report(ex3 == 765, "ex3");
#endif
    report(v3 == 345, "v3");
    report(v4 == 456, "v4");
    report(v5 == 567, "v5");
    report(v6 == 678, "v6");
#ifndef __SHARED_OBJECT__
    report(v7 == 987UL, "v7");
    report(v8 == 789, "v8");
    report(v10 == 1111, "v10");
#endif
#ifdef __DLOPEN__
    void (*external_library)() = reinterpret_cast<void(*)()>(dlsym(handle, "external_library"));
#endif
    external_library();

#ifdef __DLOPEN__
    report(*ex1_ptr == 322, "ex1 modified");
    report(*ex3_ptr == 766, "ex3 modified");
#else
    report(ex1 == 322, "ex1 modified");
    report(ex2 == 433, "ex2 modified");
    report(ex3 == 766, "ex3 modified");
#endif
    report(v1 == 124, "v1 modified");
    report(v5 == 568, "v5 modified");

    // Write on this thread's variables, and see a new thread gets
    // the original default values
    v1 = 0;
    v2 = 0;
    v3 = 0;
    v4 = 0;
    v5 = 0;
    v6 = 0;
#ifndef __SHARED_OBJECT__
    v7 = 0;
#endif

    // Try the same in a new thread
#ifdef __DLOPEN__
    std::thread t1([external_library,handle] {
#else
    std::thread t1([] {
#endif
            report(v1 == 123, "v1 in new thread");
            report(v2 == 234, "v2 in new thread");
#ifdef __DLOPEN__
            int *ex1_ptr2 = reinterpret_cast<int*>(dlsym(handle, "ex1"));
            int *ex3_ptr2 = reinterpret_cast<int*>(dlsym(handle, "ex3"));
            assert(ex1_ptr2 && ex3_ptr2);
            report(*ex1_ptr2 == 321, "ex1 in new thread");
            report(*ex3_ptr2 == 765, "ex3 in new thread");
#else
            report(ex1 == 321, "ex1 in new thread");
            report(ex2 == 432, "ex2 in new thread");
            report(ex3 == 765, "ex3 in new thread");
#endif
            report(v3 == 345, "v3 in new thread");
            report(v4 == 456, "v4 in new thread");
            report(v5 == 567, "v5 in new thread");
            report(v6 == 678, "v6 in new thread");
#ifndef __SHARED_OBJECT__
            report(v7 == 987UL, "v7 in new thread");
#endif
            external_library();
#ifdef __DLOPEN__
            report(*ex1_ptr2 == 322, "ex1 modified in new thread");
            report(*ex3_ptr2 == 766, "ex3 modified in new thread");
#else
            report(ex1 == 322, "ex1 modified in new thread");
            report(ex2 == 433, "ex2 modified in new thread");
            report(ex3 == 766, "ex3 modified in new thread");
#endif
            report(v1 == 124, "v1 modified in new thread");
            report(v5 == 568, "v5 modified in new thread");
    });
    t1.join();
#ifdef __DLOPEN__
    dlclose(handle);
#endif

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
}

#ifndef __SHARED_OBJECT__
static void before_main(void) __attribute__((constructor));
static void before_main(void)
{
    report(v7 == 987UL, "v7 in init function", true);
    report(v9 == 0, "v9 in init function", true);
}
#endif

