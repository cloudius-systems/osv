/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
// To compile on Linux, use: g++ -g -pthread -std=c++11 tests/tst-hostname.cc

#include <unistd.h>
#include <sys/utsname.h>
#include <string.h>
#include <errno.h>

#include <iostream>

static int tests = 0, fails = 0;

template<typename T>
bool do_expect(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual != expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals
                << " expected " << expecteds << "(" << expected << "), saw "
                << actual << ".\n";
        return false;
    }
    std::cout << "OK: " << file << ":" << line << ".\n";
    return true;
}
template<typename T>
bool do_expectge(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual < expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals
                << " expected >=" << expecteds << ", saw " << actual << ".\n";
        return false;
    }
    std::cout << "OK: " << file << ":" << line << ".\n";
    return true;
}
template<typename T>
bool do_expectne(T actual, T expected, const char *actuals, const char *expecteds, const char *file, int line)
{
    ++tests;
    if (actual == expected) {
        fails++;
        std::cout << "FAIL: " << file << ":" << line << ": For " << actuals
                << " expected not " << expecteds << "(" << expected << "), saw "
                << actual << ".\n";
        return false;
    }
    std::cout << "OK: " << file << ":" << line << ".\n";
    return true;
}
#define expect(actual, expected) do_expect(actual, expected, #actual, #expected, __FILE__, __LINE__)
#define expectne(actual, expected) do_expectne(actual, expected, #actual, #expected, __FILE__, __LINE__)
#define expectge(actual, expected) do_expectge(actual, expected, #actual, #expected, __FILE__, __LINE__)
#define expect_errno(call, experrno) ( \
        do_expect((long)(call), (long)-1, #call, "-1", __FILE__, __LINE__) && \
        do_expect(errno, experrno, #call " errno",  #experrno, __FILE__, __LINE__) )
#define expect_success(var, call) \
        errno = 0; \
        var = call; \
        do_expectge(var, 0, #call, "0", __FILE__, __LINE__); \
        do_expect(errno, 0, #call " errno",  "0", __FILE__, __LINE__);

int main()
{
    struct utsname origname;
    int i;
    char name[1024];
    const char *newname = "newname";

    // Test that uname() succeeds and returns a reasonable default host name
    expect_success(i, uname(&origname));
    expectne(origname.nodename[0], '\0');

    // Test that gethostname() returns the same host name as uname.
    expect_success(i, gethostname(name, sizeof(name)));
    expect(strcmp(origname.nodename, name), 0);

    // Test that we can change the hostname with sethostname().
    expect_success(i, sethostname(newname, strlen(newname)));

    // Test that gethostname() and uname() return the new name.
    expect_success(i, gethostname(name, sizeof(name)));
    expect(strcmp(name, newname), 0);
    struct utsname u;
    expect_success(i, uname(&u));
    expect(strcmp(u.nodename, newname), 0);

    // TODO: Test ENAMETOOLONG of gethostname() into too-short buffer.

    // Restore the old name
    expect_success(i, sethostname(origname.nodename, strlen(origname.nodename)));

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return fails == 0 ? 0 : 1;
}
