#include <cassert>
#include <iostream>
#include <thread>

#include <osv/run.hh>

void run_namespace()
{
    std::vector<std::string> args;
    std::shared_ptr<osv::application> app;
    int ret;

    args.push_back("tests/payload-namespace.so");

    app = osv::run("/tests/payload-namespace.so", args, &ret, true);
    assert(!ret);

}

int main(int argc, char **argv)
{
    std::thread first = std::thread(run_namespace);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::thread second = std::thread(run_namespace);
    first.join();
    second.join();
    return 0;
}
