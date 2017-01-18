/*
 * Copyright (C) 2017 ScyllaDB, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This test works on both Linux and OSv.
// To compile on Linux, use: c++ -std=c++11 tests/tst-feexcept.cc

#include <fenv.h>
#include <signal.h>
#include <assert.h>
#include <setjmp.h>
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

thread_local sigjmp_buf env;
template<typename Func>
bool sig_check(Func f, int sig) {
    // save_except works around a bug in Linux glibc
    // (https://sourceware.org/bugzilla/show_bug.cgi?id=21035) where
    // longjmp resets the FPU exception mask. So if we want it to  survive
    // the longjmp in this test, we unfortunately need to save it ourselves
    int save_except = fegetexcept();
    if (sigsetjmp(env, 1)) {
        // got the signal (and automatically restored handler)
        fedisableexcept(FE_ALL_EXCEPT);
        feenableexcept(save_except);
        return true;
    }
    struct sigaction sa;
    sa.sa_flags = SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = [] (int) {
        siglongjmp(env, 1);
    };
    assert(sigaction(sig, &sa, NULL) == 0);
    f();
    sa.sa_handler = SIG_DFL;
    assert(sigaction(sig, &sa, NULL) == 0);
    fedisableexcept(FE_ALL_EXCEPT);
    feenableexcept(save_except);
    return false;
}

int zero_i() {
    return 0;
}
double zero_d() {
    return 0.0;
}

int main(int argc, char **argv)
{
    // Test that fegetexcept() does not return a negative number
    expect(fegetexcept() >= 0, true);

    // Test that *integer* division by zero generates, ironically, a SIGFPE
    expect(sig_check([] { printf("%d\n", 1 / zero_i()); }, SIGFPE), true);

    // While, continuing the irony, by default a floating-point division by
    // zero does NOT generate a SIGFPE signal, but rather inf or nan:
    expect(sig_check([] { expect(isinf(1 / zero_d()), true); }, SIGFPE), false);
    expect(sig_check([] { expect(isnan(0.0 / zero_d()), true); }, SIGFPE), false);

    // Using feenableexcept, we can cause a division by zero to cause a
    // SIGFPE even for floating point.
    // First, let's test the FE_DIVBYZERO flag, which applies to division
    // of a non-zero by zero (which would generate an inf without this flag)
    int old = feenableexcept(FE_DIVBYZERO);
    expect(old & FE_DIVBYZERO, 0);
    expect(fegetexcept() & FE_DIVBYZERO, FE_DIVBYZERO);
    expect(sig_check([] { expect(isinf(1 / zero_d()), true); }, SIGFPE), true);
    // 0/0 isn't considered a "divide by zero" (inf) but rather an
    // "invalid operation" (nan) so FE_DIVBYZERO won't catch it. but
    // FE_INVALID would.
    expect(sig_check([] { expect(isnan(0.0 / zero_d()), true); }, SIGFPE), false);
    feenableexcept(FE_INVALID);
    expect(sig_check([] { expect(isnan(0.0 / zero_d()), true); }, SIGFPE), true);
    // enabling a bit doesn't clear the previous bit we've set
    expect(fegetexcept() & (FE_DIVBYZERO | FE_INVALID), (FE_DIVBYZERO | FE_INVALID));
    expect(sig_check([] { expect(isinf(1 / zero_d()), true); }, SIGFPE), true);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
