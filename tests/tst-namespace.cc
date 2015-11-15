#include <cassert>
#include <iostream>
#include <thread>

#include <osv/run.hh>

#include "env-common.inc"

void test_variables_isolation()
{
    std::vector<std::string> args;
    std::shared_ptr<osv::application> app;
    int ret;

    args.push_back("tests/payload-namespace.so");

    app = osv::run("/tests/payload-namespace.so", args, &ret, true);
    assert(!ret);

}

void run_variables_isolation_tests()
{
    std::thread first = std::thread(test_variables_isolation);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::thread second = std::thread(test_variables_isolation);
    first.join();
    second.join();
}

void test_namespaces_environment_isolation()
{
    std::thread t = std::thread(run_environment_payload, true);
    t.join();

    char *value = getenv("FOO");
    assert(!value);
}

int main(int argc, char **argv)
{
    run_variables_isolation_tests();

    test_namespaces_environment_isolation();

    return 0;
}
