/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Tests for printf() and related functions.
//
// To compile on Linux, use: g++ -g -std=c++11 tst-printf.cc


#include <string>
#include <iostream>

#include <stdio.h>
#include <string.h>

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

// Strangely, it appears (?) that if the format is a constant, gcc
// doesn't call our snprintf() function at all and replaces it by
// some builtin. So we need to use this variable
const char *format = "%s";

int main(int ac, char** av)
{
    // Test that when snprintf is given a >32-bit length, including -1,
    // it works rather than not copying and returning EOVERFLOW.
    // Posix mandates this EOVERFLOW, and it makes sense, but glibc
    // doesn't do it, as this test demonstrate, and neither should we.
    const char *source = "hello";
    char dest[100];
    report(snprintf(dest, -1, format, source) == 5, "snprintf with n=-1");
    report(strcmp(source, dest) == 0, "strcmp that result");
    char dest2[100];
    report(snprintf(dest2, 1ULL<<40, format, source) == 5, "snprintf with n=2^40");
    report(strcmp(source, dest2) == 0, "strcmp that result");

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
}
