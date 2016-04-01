/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Test the C++ locale support. The C++ locale framework can be used
// directly through std::locale, but is also used indirectly, e.g.,
// istream's input operator (operator>>) reads until a space character,
// defined by the locale. We mostly care about the *default* locale,
// which should behave as on Linux - e.g., the ' ' character should be
// considered a space :-)
//
// The C++ locale support is a front-end to the underlying C locale
// support. When stdlibc++ was compiled for Linux, it uses __newlocale()
// and assumes the locale internals are compatible with Linux's, so
// basically this test verifies it is indeed compatible enough.
//
// To compile on Linux, use: g++ -g -pthread -std=c++11 tests/tst-cxxlocale.cc


#include <string>
#include <iostream>
#include <sstream>
#include <locale>

static int tests = 0, fails = 0;

static void report(bool ok, std::string msg)
{
    ++tests;
    fails += !ok;
    std::cout << (ok ? "PASS" : "FAIL") << ": " << msg << "\n";
}

int main(int ac, char** av)
{
    // Verify that using an unknown locale throws an std::runtime_error,
    // and doesn't otherwise crash like it did in earlier OSv.
    bool caught = false;
    try {
        std::locale loc("NonexistantLocale");
    } catch(...) {
        caught = true;
    }
    report(caught, "nonexistant locale throws exception");

    // Verify that the isspace(' ', loc) is true for the default locale,
    // and isspace('a', loc) is false.
    std::locale loc;
    report(std::isspace(' ', loc), "std::isspace of ' ', with default locale");
    report(!std::isspace('a', loc), "std::isspace of 'a', with default locale");

    // Verify that reading a string from an in input stream only reads until
    // the first space character. This uses the default locale's isspace,
    // so this won't work if the above didn't.
    std::istringstream is("word1 word2");
    std::string s;
    is >> s;
    report(s == "word1", "s should be \"word1\"");

    // In the past, while the C functions toupper() and tolower() worked,
    // the C++ std::toupper() and std::tolower() taking the default locale
    // would crash. This also caused issue #715.
    report(toupper('B') == 'B', "C toupper");
    report(toupper('3') == '3', "C toupper");
    report(toupper('[') == '[', "C toupper");
    report(toupper('m') == 'M', "C toupper");
    report(tolower('B') == 'b', "C tolower");
    report(tolower('3') == '3', "C tolower");
    report(tolower('[') == '[', "C tolower");
    report(tolower('m') == 'm', "C tolower");
    report(std::toupper('B', loc) == 'B', "std::toupper with default locale");
    report(std::toupper('3', loc) == '3', "std::toupper with default locale");
    report(std::toupper('[', loc) == '[', "std::toupper with default locale");
    report(std::toupper('m', loc) == 'M', "std::toupper with default locale");
    report(std::tolower('B', loc) == 'b', "std::tolower with default locale");
    report(std::tolower('3', loc) == '3', "std::tolower with default locale");
    report(std::tolower('[', loc) == '[', "std::tolower with default locale");
    report(std::tolower('m', loc) == 'm', "std::tolower with default locale");

    std::cout << "SUMMARY: " << tests << " tests, " << fails << " failures\n";
    return !!fails;
}
