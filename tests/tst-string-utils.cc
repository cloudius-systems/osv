/* OSv string util tests
 *
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <cstdio>

#include <osv/string_utils.hh>

static int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

//void split(std::vector<std::string> &output, const std::string& to_split, const char *delimiters, bool compress = false);
//void replace_all(std::string &str, const std::string &from, const std::string &to);
static bool test_edge_cases()
{
    std::vector<std::string> results;

    osv::split(results, "", ",", true);
    bool test1 = results.size() == 0;

    osv::split(results, ",", ",", true);
    bool test2 = results.size() == 0;

    osv::split(results, "", ",", false);
    bool test3 = results.size() == 1 && results[0] == std::string("");

    osv::split(results, ",", ",", false);
    bool test4 = results.size() == 2 && results[0] == std::string("") && results[1] == std::string("");

    return test1 && test2 && test3 && test4;
}

static bool test_split_simple(bool compress)
{
    std::vector<std::string> results;
    osv::split(results, "first, second,,last", ",", compress);
    bool size_test = compress ? results.size() == 3 : results.size() == 4;
    bool word_tests = compress ?
           results[0] == std::string("first") &&
           results[1] == std::string(" second") &&
           results[2] == std::string("last") :
           results[0] == std::string("first") &&
           results[1] == std::string(" second") &&
           results[2] == std::string("") &&
           results[3] == std::string("last");
    return size_test && word_tests;
}

static bool test_split_simple2(bool compress)
{
    std::vector<std::string> results;
    osv::split(results, "first, second,,last", ", ", compress);
    bool size_test = compress ? results.size() == 3 : results.size() == 5;
    bool word_tests = compress ?
           results[0] == std::string("first") &&
           results[1] == std::string("second") &&
           results[2] == std::string("last") :
           results[0] == std::string("first") &&
           results[1] == std::string("") &&
           results[2] == std::string("second") &&
           results[3] == std::string("") &&
           results[4] == std::string("last");
    return size_test && word_tests;
}

static bool test_path_split()
{
    std::vector<std::string> results;
    osv::split(results, "/usr/local/bin:/usr/bin:/usr/local/sbin:/usr/sbin", ":", false);
    return results.size() == 4 &&
        results[0] == std::string("/usr/local/bin") &&
        results[1] == std::string("/usr/bin") &&
        results[2] == std::string("/usr/local/sbin") &&
        results[3] == std::string("/usr/sbin");
}

#include <boost/algorithm/string.hpp>
static bool test_path_split2(bool use_boost)
{
    std::vector<std::string> results;
    const char* path = "/usr/local/bin";
    if (use_boost) {
        boost::split(results, path, boost::is_any_of("/"));
    } else {
        osv::split(results, path, "/", false);
    }
    return results.size() == 4 &&
        results[0] == std::string("") &&
        results[1] == std::string("usr") &&
        results[2] == std::string("local") &&
        results[3] == std::string("bin");
}

static bool test_replace_all_edge_cases()
{
    std::string example1 = "";
    osv::replace_all(example1, "", "a");

    std::string example2 = " ,";
    osv::replace_all(example2, ",", "a");

    std::string example3 = " , , ";
    osv::replace_all(example3, ",", "a");

    return example1 == std::string("") && example2 == std::string(" a") && example3 == std::string(" a a ");
}

static bool test_replace_all_origin()
{
    std::string example = "$ORIGIN:/usr/local/bin:/usr/bin:$ORIGIN";
    osv::replace_all(example, "$ORIGIN", "/home");
    return example == std::string("/home:/usr/local/bin:/usr/bin:/home");
}

static bool test_replace_all_asterisk_questionmark()
{
    std::string example = "smok*,ale**,*?,??";
    osv::replace_all(example, "*", ".*");
    bool test1 = example == std::string("smok.*,ale.*.*,.*?,??");
    osv::replace_all(example, "?", ".");
    bool test2 = example == std::string("smok.*,ale.*.*,.*.,..");
    return test1 && test2;
}

int main(int argc, char *argv[])
{
    report(test_edge_cases(), "split edge cases");
    report(test_split_simple(false), "simple split");
    report(test_split_simple(true), "simple split with compress on");
    report(test_split_simple2(false), "simple split with comma and space separator");
    report(test_split_simple2(true), "simple split with comma and space separator and compress on");
    report(test_path_split(), "split path by ':'");
    report(test_path_split2(false), "split path by '/'");
    report(test_path_split2(true), "split path by '/' with boost");

    report(test_replace_all_edge_cases(), "replace_all edge cases");
    report(test_replace_all_origin(), "replace_all $ORIGIN");
    report(test_replace_all_asterisk_questionmark(), "replace_all '*' and '?'");

    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    return 0;
}
