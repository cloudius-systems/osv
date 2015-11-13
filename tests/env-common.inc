#ifndef ENV_COMMON_INC
#define ENV_COMMON_INC

#include <vector>
#include <string>
#include <unordered_map>

#include <osv/app.hh>
#include <osv/run.hh>

// the purpose of this file is to share code between tst-namespace.cc and
// tst-without-namespace.cc without merging them because we really want
// to make sure we start each tests from a fresh state.

void run_environment_payload(bool new_program)
{
    std::vector<std::string> args;
    std::shared_ptr<osv::application> app;
    int ret;

    args.push_back("tests/payload-env.so");

    app = osv::run("/tests/payload-env.so", args, &ret, new_program);
    assert(!ret);
}

void test_merging_environment(bool new_namespace)
{
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> env;
    std::shared_ptr<osv::application> app;
    int ret;

    env["SECOND"] = "MOUCHE";
    env["THIRD"] = "COCCINELLE";

    args.push_back("tests/payload-merge-env.so");

    app = osv::run("/tests/payload-merge-env.so",
                   args, &ret, new_namespace, &env);
    assert(!ret);
}


void run_merging_environment_test(bool new_namespace)
{
    setenv("FIRST", "LIBELLULE", 1);
    setenv("SECOND", "CHAMEAU", 1);
    std::thread t = std::thread(test_merging_environment, new_namespace);
    t.join();
}

#endif

