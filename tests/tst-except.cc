/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <debug.hh>
#include <exception>
#include <setjmp.h>

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    debug("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

jmp_buf env;
static bool saw_unhandled = false;
void myterminate()
{
    debug("caught unhandled exception\n");
    saw_unhandled = true;
    longjmp(env, 1);
}

int main(int ac, char** av)
{
    // Test simple throw of an integer.
    try {
        throw 1;
        report (0, "don't continue after throw");
    } catch (int e) {
        report (e == 1, "catch 1");
    }

    // Test that unhandled exceptions work and indeed call the termination
    // function as set by std::set_terminate(). Unfortunately, this test is
    // very messy, as the gcc exception handling code makes very sure an
    // unhandled exception aborts the system - after calling the termination
    // handler, if for some reason it didn't abort, it calls abort(), and
    // even catches further exceptions and aborts. So we can only escape the
    // termination handler with an ugly logjmp...
    auto old = std::set_terminate(myterminate);
    if (setjmp(env)) {
        // Second return. Success
        report(saw_unhandled, "unhandled exception\n");
    } else {
        throw 1;
        report(false, "unhandled execption\n");
    }
    std::set_terminate(old);


    debug("SUMMARY: %d tests, %d failures\n", tests, fails);

}
