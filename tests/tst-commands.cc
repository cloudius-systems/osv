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
#include <map>
#include <string.h>
#include <boost/format.hpp>

static int tests = 0, fails = 0;

namespace osv {
std::ostream& fprintf(std::ostream& os, boost::format& fmt)
{
    return os << fmt;
}
}

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

static bool test_parse_empty()
{
    std::vector<std::vector<std::string> > result;
    bool ok;

    result = osv::parse_command_line(std::string(""), ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 0) {
        return false;
    }

    return true;
}

static bool test_parse_space()
{
    std::vector<std::vector<std::string> > result;
    bool ok;

    result = osv::parse_command_line(std::string(" "), ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 0) {
        return false;
    }

    return true;
}

static bool test_parse_spaces()
{
    std::vector<std::vector<std::string> > result;
    bool ok;

    result = osv::parse_command_line(std::string(" \t\n;"), ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 0) {
        return false;
    }

    return true;
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
    std::ofstream of1("/tmp/zpool-list", std::ios::out | std::ios::binary);
    of1 << "/zpool.so list";
    of1.close();
    // Script with \n at the end
    std::ofstream of2("/tmp/myprog-prm");
    of2 << "/myprog.so prm1 \"prm2a prm2b\" prm3\n";
    of2.close();

    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "/zpool.so", "/myprog.so" };
    bool ok;

    result = osv::parse_command_line(
        std::string("runscript \"/tmp/zpool-list\"; runscript /tmp/myprog-prm  "),
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

static bool test_runscript_multiple_commands_per_line()
{
    std::ofstream of1("/tmp/myscript", std::ios::out | std::ios::binary);
    of1 << "/prog1; /prog2";
    of1.close();

    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "/prog1", "/prog2" };
    bool ok;

    result = osv::parse_command_line(
        std::string("runscript \"/tmp/myscript\""),
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

    if (result[1].size() != 2) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != cmd[i]) {
            return false;
        }
    }

    return true;
}

static bool test_runscript_multiple_commands_per_line_with_args()
{
    std::ofstream of1("/tmp/myscript", std::ios::out | std::ios::binary);
    of1 << "/prog1 pp1a ; /prog2 pp2a pp2b";
    of1.close();

    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "/prog1", "/prog2" };
    bool ok;

    result = osv::parse_command_line(
        std::string("runscript \"/tmp/myscript\""),
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

    if (result[1].size() != 4) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != cmd[i]) {
            return false;
        }
    }

    if (result[0][1] != std::string("pp1a")) {
        return false;
    }

    if (result[1][1] != std::string("pp2a")) {
        return false;
    }
    if (result[1][2] != std::string("pp2b")) {
        return false;
    }

    return true;
}

static bool test_runscript_multiple_commands_per_line_with_args_quotes()
{
    std::ofstream of1("/tmp/myscript", std::ios::out | std::ios::binary);
    of1 << "/prog1 pp1a ; /prog2 pp2a pp2b; /prog3 pp3a \"pp3b1 pp3b2\" \"pp3c1;pp3c2\" \"pp3d\" \" ;; --onx -fon;x \\t\"; ";
    of1.close();

    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "/prog1", "/prog2", "/prog3" };
    bool ok;

    result = osv::parse_command_line(
        std::string("runscript \"/tmp/myscript\""),
        ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 3) {
        return false;
    }

    if (result[0].size() != 3) {
        return false;
    }

    if (result[1].size() != 4) {
        return false;
    }

    if (result[2].size() != 7) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i][0] != cmd[i]) {
            return false;
        }
    }

    if (result[0][1] != std::string("pp1a")) {
        return false;
    }

    if (result[1][1] != std::string("pp2a")) {
        return false;
    }
    if (result[1][2] != std::string("pp2b")) {
        return false;
    }

    if (result[2][1] != std::string("pp3a")) {
        return false;
    }
    if (result[2][2] != std::string("pp3b1 pp3b2")) {
        return false;
    }
    if (result[2][3] != std::string("pp3c1;pp3c2")) {
        return false;
    }
    if (result[2][4] != std::string("pp3d")) {
        return false;
    }
    if (result[2][5] != std::string(" ;; --onx -fon;x \t")) {
        return false;
    }
    if (result[2][6] != std::string(";")) {
        return false;
    }

    return true;
}

static bool test_runscript_multiline()
{
    std::ofstream of1("/tmp/myscript", std::ios::out | std::ios::binary);
    of1 << "/prog1\n";
    of1 << "/prog2 \n";
    of1 << "  /prog3;  \n";
    of1 << "  /prog4 ;  \n";
    of1 << "  \n";
    of1 << "\n";
    of1 << "  ;  \n";
    of1 << " ; \n";
    of1.close();

    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "/prog1", "/prog2", "/prog3", "/prog4" };
    size_t expected_size[] = {2, 2, 2, 2};
    bool ok;


    result = osv::parse_command_line(
        std::string("runscript \"/tmp/myscript\";  "),
        ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 4) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i].size() != expected_size[i]) {
            return false;
        }
        if (result[i][0] != cmd[i]) {
            return false;
        }
    }
    return true;
}

static bool test_runscript_multiline_multiple_commands_per_line_with_args_quotes()
{
    std::ofstream of1("/tmp/myscript", std::ios::out | std::ios::binary);
    of1 << "/prog1 pp1a pp1b\n";
    of1 << "\t/prog2 pp2a \"pp2b1 pp2b2\" pp2c ; \n";
    of1 << "  /prog3 pp3a pp3b \"pp3c1 pp3c2\";  /prog4 \"pp4a1 pp4a2\";  \n";
    of1.close();

    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "/prog1", "/prog2", "/prog3", "/prog4" };
    size_t expected_size[] = {4, 5, 5, 3};
    bool ok;

    result = osv::parse_command_line(
        std::string("runscript \"/tmp/myscript\";  "),
        ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 4) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i].size() != expected_size[i]) {
            return false;
        }
        if (result[i][0] != cmd[i]) {
            return false;
        }
    }

    if (result[0][1] != std::string("pp1a")) {
        return false;
    }
    if (result[0][2] != std::string("pp1b")) {
        return false;
    }

    if (result[1][1] != std::string("pp2a")) {
        return false;
    }
    if (result[1][2] != std::string("pp2b1 pp2b2")) {
        return false;
    }
    if (result[1][3] != std::string("pp2c")) {
        return false;
    }

    if (result[2][1] != std::string("pp3a")) {
        return false;
    }
    if (result[2][2] != std::string("pp3b")) {
        return false;
    }
    if (result[2][3] != std::string("pp3c1 pp3c2")) {
        return false;
    }

    if (result[3][1] != std::string("pp4a1 pp4a2")) {
        return false;
    }

    return true;
}

static bool test_runscript_with_env()
{
    std::ofstream of1("/tmp/myscript", std::ios::out | std::ios::binary);
    of1 << "--env=ASDF=ttrt /prog1 pp1a pp1b\n";
    of1.close();

    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "/prog1" };
    size_t expected_size[] = {4};
    bool ok;

    if (NULL != getenv("ASDF")) {
        return false;
    }

    result = osv::parse_command_line(
        std::string("runscript \"/tmp/myscript\";  "),
        ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 1) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i].size() != expected_size[i]) {
            return false;
        }
        if (result[i][0] != cmd[i]) {
            return false;
        }
    }

    if (result[0][1] != std::string("pp1a")) {
        return false;
    }
    if (result[0][2] != std::string("pp1b")) {
        return false;
    }

    if (std::string("ttrt") != getenv("ASDF")) {
        return false;
    }

    unsetenv("ASDF");

    return true;
}

static bool test_runscript_with_env_in_script()
{
    std::ofstream of1("/tmp/myscript", std::ios::out | std::ios::binary);
    of1 << "--env=ASDF=ttrt --env=PORT=$THEPORT /$THEPROG pp1a $PRM1b pp1c $PRM1d\n";
    of1.close();

    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "/prog1" };
    size_t expected_size[] = {6};
    bool ok;

    // those two are set during command parsing
    if (NULL != getenv("ASDF")) {
        return false;
    }
    if (NULL != getenv("PORT")) {
        return false;
    }
    // those are required during command parsing
    if (0 != setenv("THEPORT", "4321", 1)) {
        return false;
    }
    if (0 != setenv("THEPROG", "prog1", 1)) {
        return false;
    }
    if (0 != setenv("PRM1b", "pp1b", 1)) {
        return false;
    }
    if (0 != setenv("PRM1d", "pp1d", 1)) {
        return false;
    }

    result = osv::parse_command_line(
        std::string("runscript \"/tmp/myscript\";  "),
        ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 1) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i].size() != expected_size[i]) {
            return false;
        }
        if (result[i][0] != cmd[i]) {
            return false;
        }
    }

    if (result[0][1] != std::string("pp1a")) {
        return false;
    }
    if (result[0][2] != std::string("pp1b")) {
        return false;
    }
    if (result[0][3] != std::string("pp1c")) {
        return false;
    }
    if (result[0][4] != std::string("pp1d")) {
        return false;
    }

    if (std::string("ttrt") != getenv("ASDF")) {
        return false;
    }
    if (std::string("4321") != getenv("PORT")) {
        return false;
    }

    unsetenv("ASDF");
    unsetenv("PORT");
    unsetenv("THEPORT");
    unsetenv("THEPROG");
    unsetenv("PRM1b");
    unsetenv("PRM1d ");

    return true;
}

static bool test_runscript_with_conditional_env_in_script(bool set_env_vars_before_evaluation)
{
    std::ofstream of1("/tmp/myscript", std::ios::out | std::ios::binary);
    of1 << "--env=ASDF?=ttrt --env=PORT?=$THEPORT /$THEPROG pp1a $PRM1b pp1c $PRM1d\n";
    of1.close();

    std::vector<std::vector<std::string> > result;
    std::vector<std::string> cmd = { "/prog1" };
    std::map<std::string, std::string> expected_vars;
    size_t expected_size[] = {6};
    bool ok;

    // those two are set during command parsing
    if (NULL != getenv("ASDF")) {
        return false;
    }
    if (NULL != getenv("PORT")) {
        return false;
    }
    // those are required during command parsing
    if (0 != setenv("THEPORT", "4321", 1)) {
        return false;
    }
    if (0 != setenv("THEPROG", "prog1", 1)) {
        return false;
    }
    if (0 != setenv("PRM1b", "pp1b", 1)) {
        return false;
    }
    if (0 != setenv("PRM1d", "pp1d", 1)) {
        return false;
    }
    // run test with conditional variables set or clear
    if (set_env_vars_before_evaluation) {
        expected_vars["ASDF"] = "asdf-old";
        expected_vars["PORT"] = "port-old";
        if (0 != setenv("ASDF", expected_vars["ASDF"].c_str(), 1)) {
            return false;
        }
        if (0 != setenv("PORT", expected_vars["PORT"].c_str(), 1)) {
            return false;
        }
    }
    else {
        expected_vars["ASDF"] = "ttrt";
        expected_vars["PORT"] = "4321";
    }

    result = osv::parse_command_line(
        std::string("runscript \"/tmp/myscript\";  "),
        ok);

    if (!ok) {
        return false;
    }

    if (result.size() != 1) {
        return false;
    }

    for (size_t i = 0; i < result.size(); i++) {
        if (result[i].size() != expected_size[i]) {
            return false;
        }
        if (result[i][0] != cmd[i]) {
            return false;
        }
    }

    if (result[0][1] != std::string("pp1a")) {
        return false;
    }
    if (result[0][2] != std::string("pp1b")) {
        return false;
    }
    if (result[0][3] != std::string("pp1c")) {
        return false;
    }
    if (result[0][4] != std::string("pp1d")) {
        return false;
    }

    // environ variable with ? in name should not be created
    if (nullptr != getenv("ASDF?")) {
        return false;
    }
    if (nullptr != getenv("PORT?")) {
        return false;
    }
    // std::string(NULL) is undefined behavior, hence check getenv returns a valid string.
    if (nullptr == getenv("ASDF")) {
        return false;
    }
    if (nullptr == getenv("PORT")) {
        return false;
    }

    if (expected_vars["ASDF"] != getenv("ASDF")) {
        return false;
    }
    if (expected_vars["PORT"] != getenv("PORT")) {
        return false;
    }

    unsetenv("ASDF");
    unsetenv("PORT");
    unsetenv("THEPORT");
    unsetenv("THEPROG");
    unsetenv("PRM1b");
    unsetenv("PRM1d ");

    return true;
}

bool test_loader_parse_cmdline(const char* instr, std::vector<std::string> ref_argv, const char* ref_app_cmdline) {
    char *str, *str_to_be_freed;
    // strdup alternative code might catch read beyond end of str string.
    // The strdup above might contains \0 beyond terminating '\0', so say
    // strlen(str+strlen(str) + 1) still returns 0, or some random number
    // instead of scanning random garbage.
#if 0
    *str = strdup(instr);
    str_to_be_freed = str;
#else
    int str_length = std::max(strlen(instr), 1024ul);
    str = (char*)malloc(str_length*9);
    str_to_be_freed = str;
    memset(str, 'X', str_length*9);
    str += str_length*4;
    strcpy(str, instr);
#endif

    int argc;
    char** argv;
    char *app_cmdline;

    //printf("/*-------------------------------------*/\n");
    //printf("str = %p '%s'\n", str, str);
    osv::loader_parse_cmdline(str, &argc, &argv, &app_cmdline);

    // print and check result
    char **ch;
    int ii;
    int old_len = 0;
    if (argc != (int)ref_argv.size()) {
        return false;
    }
    for (ii = 0, ch = argv; ch != nullptr && *ch != nullptr; ii++, ch++) {
        //printf("  argv[%d] = %p '%s', expected = '%s'\n", ii, *ch, *ch, ref_argv[ii].c_str());
        if (ref_argv[ii] != argv[ii]) {
            return false;
        }
        if(ii>0) {
            // check that argv strings are consecutive in memory
            // not really needed for loader options, but common implementation
            // detail for Linux app main(argc, argv).
            if ((argv[ii-1] + old_len) != argv[ii]) {
                return false;
            }
        }
        old_len = strlen(argv[ii]) + 1; // num of bytes including terminating null.
    }

    //printf("  ii = %d, ref_argv.size()=%d\n", ii, (int)ref_argv.size());
    if (ii != argc) {
        return false;
    }
    //printf("  app_cmdline=%p '%s', expected = '%s'\n", app_cmdline, app_cmdline, ref_app_cmdline);
    if (std::string(app_cmdline) != ref_app_cmdline) {
        return false;
    }

    free(argv);
    free(str_to_be_freed);
    //printf("/*-------------------------------------*/\n");
    return true;
}

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)

void all_test_loader_parse_cmdline() {
    // empty
    report(test_loader_parse_cmdline("", {}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline(" ", {}, " "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("  ", {}, "  "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    //
    report(test_loader_parse_cmdline("-", {"-"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--", {"--"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("-- ", {"--"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--  ", {"--"}, " "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--   ", {"--"}, "  "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);

    report(test_loader_parse_cmdline("aa", {}, "aa"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline(" aa", {}, " aa"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("aa ", {}, "aa " ),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline(" aa ", {}, " aa "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("  aa  ", {}, "  aa  "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    //
    report(test_loader_parse_cmdline("--aa", {"--aa"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline(" --aa", {"--aa"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--aa ", {"--aa"}, "" ),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline(" --aa ", {"--aa"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("  --aa  ", {"--aa"}, " "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);

    report(test_loader_parse_cmdline("aa bb", {}, "aa bb"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("aa  bb", {}, "aa  bb"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("aa   bb", {}, "aa   bb"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("aa    bb", {}, "aa    bb"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    //
    report(test_loader_parse_cmdline("--aa --bb", {"--aa", "--bb"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--aa  --bb", {"--aa", "--bb"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--aa   --bb", {"--aa", "--bb"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--aa    --bb", {"--aa", "--bb"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);

    report(test_loader_parse_cmdline("aa bb ", {}, "aa bb "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("aa bb  ", {}, "aa bb  "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("aa bb   ", {}, "aa bb   "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    //
    report(test_loader_parse_cmdline("--aa --bb ", {"--aa", "--bb"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--aa --bb  ", {"--aa", "--bb"}, " "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--aa --bb   ", {"--aa", "--bb"}, "  "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);

    report(test_loader_parse_cmdline(" aa bb", {}, " aa bb"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("  aa bb", {}, "  aa bb"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("   aa bb", {}, "   aa bb"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    //
    report(test_loader_parse_cmdline(" --aa --bb", {"--aa", "--bb"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("  --aa --bb", {"--aa", "--bb"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("   --aa --bb", {"--aa", "--bb"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);

    report(test_loader_parse_cmdline(" aa bb ", {}, " aa bb "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("  aa bb  ", {}, "  aa bb  "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("   aa bb   ", {}, "   aa bb   "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    //
    report(test_loader_parse_cmdline(" --aa --bb ", {"--aa", "--bb"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("  --aa --bb  ", {"--aa", "--bb"}, " "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("   --aa --bb   ", {"--aa", "--bb"}, "  "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);

    report(test_loader_parse_cmdline("--aa --bb cc", {"--aa", "--bb"}, "cc"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline(" --aa --bb cc", {"--aa", "--bb"}, "cc"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("  --aa --bb  cc", {"--aa", "--bb"}, " cc"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("   --aa --bb   cc", {"--aa", "--bb"}, "  cc"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);

    report(test_loader_parse_cmdline("aa \"bb\" cc", {}, "aa \"bb\" cc"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("aa bb\\ cc dd", {}, "aa bb\\ cc dd"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("aa bb\\ \\ cc dd", {}, "aa bb\\ \\ cc dd"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    //
    report(test_loader_parse_cmdline("--aa --\"bb\" --cc", {"--aa", "--\"bb\"", "--cc"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--aa \"bb\" --cc", {"--aa"}, "\"bb\" --cc"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--aa --bb\\ \\ cc --dd", {"--aa", "--bb  cc", "--dd"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--aa --bb\\ \\ --cc --dd", {"--aa", "--bb  --cc", "--dd"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--aa --bb\\ cc --dd", {"--aa", "--bb cc", "--dd"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--aa --bb\\ --cc --dd", {"--aa", "--bb --cc", "--dd"}, ""),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);

    // and realistic/valid OSv cmdline example
    report(test_loader_parse_cmdline("--env=AA=aa  --env=BB=bb1\\ bb2   --env=CC=cc1\\ \\ cc2\\ cc3 prog arg1 \"arg2a arg2b\" arg3",
        {"--env=AA=aa", "--env=BB=bb1 bb2", "--env=CC=cc1  cc2 cc3"}, "prog arg1 \"arg2a arg2b\" arg3"),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
    report(test_loader_parse_cmdline("--env=AA=aa  --env=BB=bb1\\ bb2   --env=CC=cc1\\ \\ cc2\\ cc3   prog  arg1 \"arg2a  arg2b\"  arg3  ",
        {"--env=AA=aa", "--env=BB=bb1 bb2", "--env=CC=cc1  cc2 cc3"}, "  prog  arg1 \"arg2a  arg2b\"  arg3  "),
        "TEST=loader_parse_cmdline:LINE=" LINE_STRING);
}

int main(int argc, char *argv[])
{
    all_test_loader_parse_cmdline();

    report(test_parse_empty(), "empty string");
    report(test_parse_space(), "single space");
    report(test_parse_spaces(), "multiple diffrent whitespaces");
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
    report(test_runscript_multiple_commands_per_line(),
           "runscript multiple commands per line");
    report(test_runscript_multiple_commands_per_line_with_args(),
           "runscript multiple commands per line with args");
    report(test_runscript_multiple_commands_per_line_with_args_quotes(),
           "runscript multiple commands per line with args and quotes");
    report(test_runscript_multiline(),
           "runscript multiple lines");
    report(test_runscript_multiline_multiple_commands_per_line_with_args_quotes(),
           "runscript multiple lines, multiple commands per line, with args and quotes");
    report(test_runscript_with_env(),
           "runscript with --env");
    report(test_runscript_with_env_in_script(),
           "runscript with --env in script");
    report(test_runscript_with_conditional_env_in_script(false),
           "runscript with --env in script, makefile conditional syntax"
           ", values unset before runscript");
    report(test_runscript_with_conditional_env_in_script(true),
           "runscript with --env in script, makefile conditional syntax"
           ", values set before runscript");
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
