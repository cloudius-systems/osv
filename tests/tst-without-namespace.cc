#include <cassert>
#include <iostream>
#include <thread>

#include <osv/run.hh>

#include "env-common.inc"

void test_variables_sharing(bool must_succeed)
{
    std::vector<std::string> args;
    std::shared_ptr<osv::application> app;
    int ret;

    args.push_back("tests/payload-namespace.so");

    app = osv::run("/tests/payload-namespace.so", args, &ret);
    assert(!ret == must_succeed);

}

void run_variable_sharing_tests()
{
    std::thread first = std::thread(test_variables_sharing, true);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::thread second = std::thread(test_variables_sharing, false);
    first.join();
    second.join();
}

void test_environment_sharing()
{
    std::thread t = std::thread(run_environment_payload, false);
    t.join();

    char *value = getenv("FOO");
    assert(std::string(value) == std::string("BAR"));
}

int main(int argc, char **argv)
{
    run_variable_sharing_tests();

    test_environment_sharing();

    run_merging_environment_test(false);

    return 0;
}
