/* OSv command line options parsing tests
 *
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <cstdio>

#include <fstream>
#include <map>
#include <string.h>
#include <iostream>
#include <functional>
#include <osv/options.hh>
#include <cassert>

static int tests = 0, fails = 0;

using namespace std;
using namespace options;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

static void handle_parse_error(const string &message)
{
    cout << message << endl;
    abort();
}

static bool test_parse_empty()
{
    vector<const char*> argv = {};
    auto options = parse_options_values(argv.size(), (char**)argv.data(), handle_parse_error, false);
    return options.empty();
}

static bool test_parse_non_option()
{
    vector<const char*> argv = {"--verbose", "olo"};

    bool non_option_detected = false;
    parse_options_values(argv.size(), (char**)argv.data(),[&non_option_detected](const std::string &message) {
        assert(message == "not an option: olo");
        non_option_detected = true;
    }, false);

    return non_option_detected;
}

static bool test_parse_single_option_flag()
{
    vector<const char*> argv = {"--verbose"};

    auto options = parse_options_values(argv.size(), (char**)argv.data(), handle_parse_error, false);
    assert(options.size() == 1);

    assert(!options::extract_option_flag(options, "bogus", handle_parse_error));
    assert(options::extract_option_flag(options, "verbose", handle_parse_error));

    return options.empty();
}

static bool test_parse_and_extract_non_flag_option()
{
    vector<const char*> argv = {"--enabled=1"};

    auto options = parse_options_values(argv.size(), (char**)argv.data(), handle_parse_error, false);
    assert(options.size() == 1);
    assert(options::option_value_exists(options, "enabled"));

    bool non_flag_detected = false;
    options::extract_option_flag(options, "enabled", [&non_flag_detected](const std::string &message) {
        assert(message == "option '--enabled' does not take any arguments");
        non_flag_detected = true;
    });

    return non_flag_detected;
}

static bool test_parse_single_option_value()
{
    vector<const char*> argv = {"--console=bla"};

    auto options = parse_options_values(argv.size(), (char**)argv.data(), handle_parse_error, false);
    assert(options.size() == 1);

    assert(options::option_value_exists(options, "console"));
    assert(options::extract_option_value(options, "console") == "bla");

    return options.empty();
}

static bool test_parse_option_with_missing_value()
{
    vector<const char*> argv = {"--console="};

    bool missing_detected = false;
    parse_options_values(argv.size(), (char**)argv.data(), [&missing_detected](const std::string &message) {
        assert(message == "the required argument for option '--console' is missing");
        missing_detected = true;
    }, false);

    return missing_detected;
}

static bool test_parse_single_option_multiple_values()
{
    vector<const char*> argv = {"--env=bla1", "--env=A=bla2"};

    auto options = parse_options_values(argv.size(), (char**)argv.data(), handle_parse_error, false);
    assert(options.size() == 1);
    assert(options["env"].size() == 2);

    assert(options::option_value_exists(options, "env"));
    auto values = options::extract_option_values(options, "env");
    assert(values.size() == 2);
    assert(values[0] == "bla1");
    assert(values[1] == "A=bla2");

    return options.empty();
}

static bool test_parse_multiple_options()
{
    vector<const char*> argv = {"--env=bla1", "--console=bla", "--env=A=bla2", "--verbose"};

    auto options = parse_options_values(argv.size(), (char**)argv.data(), handle_parse_error, false);
    assert(options.size() == 3);
    assert(options["env"].size() == 2);

    assert(options::extract_option_flag(options, "verbose", handle_parse_error));

    assert(options::option_value_exists(options, "env"));
    auto values = options::extract_option_values(options, "env");
    assert(values.size() == 2);
    assert(values[0] == "bla1");
    assert(values[1] == "A=bla2");

    assert(options::option_value_exists(options, "console"));
    assert(options::extract_option_value(options, "console") == "bla");

    return options.empty();
}

static bool test_parse_option_flag_conflict()
{
    vector<const char*> argv = {"--verbose", "--verbose=bla"};

    bool conflict_detected = false;
    parse_options_values(argv.size(), (char**)argv.data(),[&conflict_detected](const std::string &message) {
        assert(message == "duplicate option: --verbose=bla");
        conflict_detected = true;
    }, false);

    return conflict_detected;
}

static bool test_parse_option_flag_conflict2()
{
    vector<const char*> argv = {"--verbose=bla", "--verbose" };

    bool conflict_detected = false;
    parse_options_values(argv.size(), (char**)argv.data(),[&conflict_detected](const std::string &message) {
        assert(message == "duplicate option: --verbose");
        conflict_detected = true;
    }, false);

    return conflict_detected;
}

static bool test_parse_single_int_option_value()
{
    vector<const char*> argv = {"--delay=15"};

    auto options = parse_options_values(argv.size(), (char**)argv.data(), handle_parse_error, false);
    assert(options.size() == 1);

    assert(options::option_value_exists(options, "delay"));
    assert(options::extract_option_int_value(options, "delay", handle_parse_error) == 15);

    return options.empty();
}

static bool test_parse_invalid_int_option_value()
{
    vector<const char*> argv = {"--delay=ola"};

    auto options = parse_options_values(argv.size(), (char**)argv.data(), handle_parse_error, false);
    assert(options.size() == 1);

    bool invalid_int_detected = false;
    options::extract_option_int_value(options, "delay", [&invalid_int_detected](const std::string &message) {
        assert(message == "the argument ('ola') for option '--delay' is invalid");
        invalid_int_detected = true;
    });

    return invalid_int_detected;
}

static bool test_parse_single_float_option_value()
{
    vector<const char*> argv = {"--var=1.05"};

    auto options = parse_options_values(argv.size(), (char**)argv.data(), handle_parse_error, false);
    assert(options.size() == 1);

    assert(options::option_value_exists(options, "var"));
    assert(options::extract_option_float_value(options, "var", handle_parse_error) == 1.05f);

    return options.empty();
}

static bool test_parse_multiple_options_with_separate_value()
{
    vector<const char*> argv =
            {"--log", "debug", "--env=bla1", "--console=bla", "--env=A=bla2", "--verbose", "--on", "--count", "1"};

    auto options = parse_options_values(argv.size(), (char**)argv.data(), handle_parse_error, true);
    assert(options.size() == 6);
    assert(options["env"].size() == 2);

    assert(options::extract_option_flag(options, "verbose", handle_parse_error));
    assert(options::extract_option_flag(options, "on", handle_parse_error));

    assert(options::option_value_exists(options, "env"));
    auto values = options::extract_option_values(options, "env");
    assert(values.size() == 2);
    assert(values[0] == "bla1");
    assert(values[1] == "A=bla2");

    assert(options::option_value_exists(options, "log"));
    assert(options::extract_option_value(options, "log") == "debug");

    assert(options::option_value_exists(options, "count"));
    assert(options::extract_option_int_value(options, "count", handle_parse_error) == 1);

    assert(options::option_value_exists(options, "console"));
    assert(options::extract_option_value(options, "console") == "bla");

    return options.empty();
}

int main(int argc, char *argv[])
{
    report(test_parse_empty(), "empty string");
    report(test_parse_non_option(), "non option");
    report(test_parse_single_option_flag(), "single option flag");
    report(test_parse_and_extract_non_flag_option(), "non-flag option with value");
    report(test_parse_single_option_value(), "single option value");
    report(test_parse_option_with_missing_value(), "single option with missing value");
    report(test_parse_single_option_multiple_values(), "single option multiple values");
    report(test_parse_multiple_options(), "multiple options");
    report(test_parse_option_flag_conflict(), "option flag conflict");
    report(test_parse_option_flag_conflict2(), "2nd option flag conflict");
    report(test_parse_single_int_option_value(), "single int option value");
    report(test_parse_single_float_option_value(), "single float option value");
    report(test_parse_multiple_options_with_separate_value(), "multiple options with separated values");
    report(test_parse_invalid_int_option_value(), "invalid int option");

    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
