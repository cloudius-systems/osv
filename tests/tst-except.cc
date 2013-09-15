/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <debug.hh>

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    debug("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

int main(int ac, char** av)
{
    try {
        throw 1;
        report (0, "don't continue after throw");
    } catch (int e) {
        report (e == 1, "catch 1");
    }
    debug("SUMMARY: %d tests, %d failures\n", tests, fails);

}
