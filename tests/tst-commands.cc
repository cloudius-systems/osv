/* OSv command line parsing tests
 *
 * Copyright (C) 2013 Nodalink, SARL.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <cstdio>

#include <osv/commands.hh>

static int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

static bool test_parse_simplest()
{
    std::vector<std::vector<std::string> > result;
    bool ok;

    result = osv::parse_command_line(std::string("mkfs.so"), ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 1) {
        return false;
    }

    if (result[0].size() != 1) {
        return false;
    }

    if (result[0][0] != std::string("mkfs.so")) {
        return false;
    }

    return true;
}

#include <iostream>

static bool test_parse_simplest_with_args()
{
    std::vector<std::vector<std::string> > result;
    bool ok;

    result = osv::parse_command_line(std::string("mkfs.so --blub      --blah"),
                                     ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 1) {
        return false;
    }

    if (result[0].size() != 3) {
        return false;
    }

    if (result[0][0] != std::string("mkfs.so")) {
        return false;
    }

    if (result[0][1] != std::string("--blub")) {
        return false;
    }

    if (result[0][2] != std::string("--blah")) {
        return false;
    }

    return true;
}

static bool test_parse_simplest_with_quotes()
{
    std::vector<std::vector<std::string> > result;
    bool ok;

    result = osv::parse_command_line(
                 std::string("mkfs.so  \"--blub ;  --blah\""),
                 ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 1) {
        return false;
    }

    if (result[0].size() != 2) {
        return false;
    }

    if (result[0][0] != std::string("mkfs.so")) {
        return false;
    }

    if (result[0][1] != std::string("--blub ;  --blah")) {
        return false;
    }

    return true;
}

static bool test_parse_simple_multiple()
{
    std::vector<std::vector<std::string> > result;
    std::vector<std::string> res = { "mkfs.so", "cpiod.so", "haproxy.so" };
    bool ok;

    result = osv::parse_command_line(
                 std::string("mkfs.so;cpiod.so   ;   haproxy.so;"),
                 ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 3) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i].size() != 1) {
            return false;
        }
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != res[i]) {
            return false;
        }
    }

    return true;
}

static bool test_parse_multiple_with_args()
{
    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "mkfs.so", "cpiod.so", "haproxy.so" };
    bool ok;

    result = osv::parse_command_line(
                 std::string("mkfs.so;cpiod.so  --onx  ;   haproxy.so;"),
                 ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 3) {
        return false;
    }

    if (result[0].size() != 1) {
        return false;
    }

    if (result[1].size() != 2) {
        return false;
    }

    if (result[2].size() != 1) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != cmd[i]) {
            return false;
        }
    }

    if (result[1][1] != std::string("--onx")) {
        return false;
    }

    return true;
}

static bool test_parse_multiple_with_quotes()
{
    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "mkfs.so", "cpiod.so", "haproxy.so" };
    bool ok;

    result = osv::parse_command_line(
        std::string("mkfs.so;cpiod.so  \" ;; --onx -fon;x \\t\" ;   haproxy.so"),
        ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 3) {
        return false;
    }

    if (result[0].size() != 1) {
        return false;
    }

    if (result[1].size() != 2) {
        return false;
    }

    if (result[2].size() != 1) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != cmd[i]) {
            return false;
        }
    }

    if (result[1][1] != std::string(" ;; --onx -fon;x \t")) {
        return false;
    }

    return true;
}

static bool test_cpiod_haproxy()
{
    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "/tools/cpiod.so", "/usr/haproxy.so" };
    bool ok;

    result = osv::parse_command_line(
        std::string("/tools/cpiod.so;/usr/haproxy.so -f /usr/haproxy.conf    "),
        ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 2) {
        return false;
    }

    if (result[0].size() != 1) {
        return false;
    }

    if (result[1].size() != 3) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != cmd[i]) {
        return false;
            return false;
        }
    }

    if (result[1][1] != std::string("-f")) {
        return false;
    }

    if (result[1][2] != std::string("/usr/haproxy.conf")) {
        return false;
    }

    return true;
}

int main(int argc, char *argv[])
{
    report(test_parse_simplest(), "simplest command line");
    report(test_parse_simplest_with_args(), "simplest command line with args");
    report(test_parse_simplest_with_quotes(),
           "simplest command line with quotes");
    report(test_parse_simple_multiple(),
           "simple multiple commands");
    report(test_parse_multiple_with_args(),
           "simple multiple commands with args");
    report(test_parse_multiple_with_quotes(),
           "simple multiple commands with quotes");
    report(test_cpiod_haproxy(),
           "cpiod upload and haproxy launch");
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
