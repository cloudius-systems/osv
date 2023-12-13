/*
 * Copyright (C) 2017 ScyllaDB, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This test works on both Linux and OSv.
// To compile on Linux, use: c++ -std=c++11 tests/tst-sigaltstack.cc

#include <signal.h>
#include <assert.h>
#include <setjmp.h>
#include <sys/mman.h>

#include <iostream>
#ifndef __OSV__
extern "C" int __sigsetjmp(sigjmp_buf env, int savemask);
#define sigsetjmp(env, savemask) __sigsetjmp (env, savemask)
#endif

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
    if (sigsetjmp(env, 1)) {
        // got the signal (and automatically restored handler)
        return true;
    }
    struct sigaction sa;
    // Note: we use SA_ONSTACK to have this run on the sigaltstack() stack
    sa.sa_flags = SA_RESETHAND | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = [] (int) {
        siglongjmp(env, 1);
    };
    assert(sigaction(sig, &sa, NULL) == 0);
    f();
    sa.sa_handler = SIG_DFL;
    assert(sigaction(sig, &sa, NULL) == 0);
    return false;
}

// endless_recursive() recurses ad infinitum, so it will finish the entire
// stack and finally reach an unmapped area and cause a SIGSEGV. When it
// does, a SIGSEGV handler will not be able to run on the normal thread stack
// (which ran out), and only if sigaltstack() is properly supported, will the
// signal handler be able to run.
int endless_recursive() {
    return endless_recursive() + endless_recursive();
}

int main(int argc, char **argv)
{
    // setup the per-thread sigaltstack():
    stack_t sigstack;
    sigstack.ss_size = SIGSTKSZ;
    sigstack.ss_sp = malloc(sigstack.ss_size);
    sigstack.ss_flags = 0;

    // Enable the sigaltstack. Being able to catch SIGSEGV after
    // endless_recursive() - as we do below - will not work without it
    stack_t oldstack {};
    expect(sigaltstack(&sigstack, &oldstack), 0);
    // We didn't have a sigaltstack previously, so oldstack is just disabled
    expect(oldstack.ss_flags, (int)SS_DISABLE);

    expect(sig_check(endless_recursive, SIGSEGV), true);

    // Check that disabling a stack works
    sigstack.ss_flags = SS_DISABLE;
    oldstack = {};
    expect(sigaltstack(&sigstack, &oldstack), 0);
    // The previous stack was the one we had
    expect(oldstack.ss_sp, sigstack.ss_sp);
    expect(oldstack.ss_size, sigstack.ss_size);

    free(sigstack.ss_sp);

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
