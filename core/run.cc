#include <string.h>
#include <algorithm>
#include <vector>
#include <boost/range/algorithm/transform.hpp>

#include <elf.hh>
#include <osv/run.hh>

#include <debug.hh>

using namespace std;
using namespace boost::range;

namespace osv {

bool run(std::string path, int argc, char** argv, int *return_code)
{
    // Load the given shared library. When "lib" goes out of scope, it
    // may be unloaded.
    auto lib = elf::get_program()->get_library(path);
    if (!lib) {
        return false;
    }
    auto main = lib->lookup<int (int, char**)>("main");
    if (!main) {
        return false;
    }
    int rc = main(argc, argv);
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
