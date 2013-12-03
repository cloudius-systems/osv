#include <string.h>
#include <algorithm>
#include <vector>
#include <boost/range/algorithm/transform.hpp>

#include <elf.hh>
#include <osv/run.hh>

#include <debug.hh>

extern int optind;

using namespace std;
using namespace boost::range;

// Java uses this global variable (supplied by Glibc) to figure out the
// initial thread's stack end.
void *__libc_stack_end;
bool static stack_end_init;

namespace osv {

bool run(std::string path, int argc, char** argv, int *return_code)
{
    // Ensure that the shared library doesn't exit when we return by
    // keeping a reference to it in the free store.
    auto lib = *(new std::shared_ptr<elf::object>(elf::get_program()->get_library(path)));

    if (!lib) {
        return false;
    }
    auto main = lib->lookup<int (int, char**)>("main");
    if (!main) {
        return false;
    }
    // make sure to have a fresh optind across calls
    // FIXME: fails if run() is executed in parallel
    int old_optind = optind;
    optind = 0;
    if (!stack_end_init) {
        __libc_stack_end = __builtin_frame_address(0);
        stack_end_init = true;
    }
    int rc = main(argc, argv);
    optind = old_optind;
    if (return_code) {
        *return_code = rc;
    }
    return true;
}

bool run(string path, vector<string> args, int* return_code)
{
    // C main wants mutable arguments, so we have can't use strings directly
    vector<vector<char>> mut_args;
    transform(args, back_inserter(mut_args),
            [](string s) { return vector<char>(s.data(), s.data() + s.size() + 1); });
    vector<char*> argv;
    transform(mut_args.begin(), mut_args.end(), back_inserter(argv),
            [](vector<char>& s) { return s.data(); });
    auto argc = argv.size();
    argv.push_back(nullptr);
    return run(path, argc, argv.data(), return_code);
}

}
