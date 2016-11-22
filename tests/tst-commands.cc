/* OSv command line parsing tests
 *
 * Copyright (C) 2013 Nodalink, SARL.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <cstdio>

#include <osv/commands.hh>
#include <fstream>

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

    if (result[0].size() != 2) {
        return false;
    }

    if (result[0][0] != std::string("mkfs.so")) {
        return false;
    }

    if (result[0][1] != std::string("")) {
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

    if (result[0].size() != 4) {
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

    if (result[0][3] != std::string("")) {
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

    if (result[0].size() != 3) {
        return false;
    }

    if (result[0][0] != std::string("mkfs.so")) {
        return false;
    }

    if (result[0][1] != std::string("--blub ;  --blah")) {
        return false;
    }

    if (result[0][2] != std::string("")) {
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
        if (result[i].size() != 2) {
            return false;
        }
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != res[i]) {
            return false;
        }
        if (result[i][1] != std::string(";")) {
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

    if (result[0].size() != 2) {
        return false;
    }

    if (result[1].size() != 3) {
        return false;
    }

    if (result[2].size() != 2) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != cmd[i]) {
            return false;
        }
        if (result[i][result[i].size()-1] != std::string(";")) {
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

    if (result[0].size() != 2) {
        return false;
    }

    if (result[1].size() != 3) {
        return false;
    }

    if (result[2].size() != 2) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != cmd[i]) {
            return false;
        }
        if (result[i][result[i].size()-1] != std::string(";") &&
                result[i][result[i].size()-1] != std::string("")) {
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

    if (result[0].size() != 2) {
        return false;
    }

    if (result[1].size() != 4) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != cmd[i]) {
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

static bool test_runscript_multiple_with_args_quotes()
{
    // Script without \n at the end
    std::ofstream of1("/zpool-list", std::ios::out | std::ios::binary);
    of1 << "/zpool.so list";
    of1.close();
    // Script with \n at the end
    std::ofstream of2("/myprog-prm");
    of2 << "/myprog.so prm1 \"prm2a prm2b\" prm3\n";
    of2.close();

    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "/zpool.so", "/myprog.so" };
    bool ok;

    result = osv::parse_command_line(
        std::string("runscript \"/zpool-list\"; runscript /myprog-prm  "),
        ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 2) {
        return false;
    }

    if (result[0].size() != 3) {
        return false;
    }

    if (result[1].size() != 5) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != cmd[i]) {
            return false;
        }
    }

    if (result[1][1] != std::string("prm1")) {
        return false;
    }
    if (result[1][2] != std::string("prm2a prm2b")) {
        return false;
    }
    if (result[1][3] != std::string("prm3")) {
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
    report(test_runscript_multiple_with_args_quotes(),
           "runscript multiple with args and quotes");
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
