#ifndef ENV_COMMON_INC
#define ENV_COMMON_INC

#include <vector>
#include <string>

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

#endif

