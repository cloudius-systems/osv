/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// To compile on Linux, use: g++ -g -pthread -std=c++11 tst-regex.cc

#include <iostream>

#include <sys/types.h>
#include <regex.h>

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

int main(int argc, char** argv)
{
    // Test that despite logic, Linux's regoff_t is 4 bytes, not 8 bytes, so
    // ours should be the same, if we want to be compatible with the Linux
    // ABI (so we can run programs compiled for Linux).
    report(sizeof(regoff_t) == 4, "sizeof(regoff_t) should 4, like in Linux");

    // Try that a simple regular expression usage works.
    regex_t reg;
    report(regcomp(&reg, "my name is ([a-zA-Z]*).*", REG_EXTENDED) == 0, "regcomp successful");
    regmatch_t matches[2];
    report(regexec(&reg, "my name is Nadav!", 2, matches, 0) == 0, "regexec successful");
    report(matches[0].rm_so == 0, "first match start-offset");
    report(matches[0].rm_eo == 17, "first match end-offset");
    report(matches[1].rm_so == 11, "second match start-offset");
    report(matches[1].rm_eo == 16, "second match end-offset");

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
}
