#include <string.h>
#include <algorithm>
#include <vector>
#include <boost/range/algorithm/transform.hpp>

#include <osv/elf.hh>
#include <osv/run.hh>

#include <osv/debug.hh>
#include <errno.h>
#include <libgen.h>

extern int optind;

using namespace std;
using namespace boost::range;

namespace osv {

std::shared_ptr<elf::object> run(std::string path,
                                 int argc, char** argv, int *return_code)
{
    std::shared_ptr<elf::object> lib;

    try {
        lib = elf::get_program()->get_library(path);
    } catch(std::exception &e) {
        // TODO: expose more information about the failure.
        return nullptr;
    }

    if (!lib) {
        return nullptr;
    }
    auto main = lib->lookup<int (int, char**)>("main");
    if (!main) {
        return nullptr;
    }

    char *c_path = (char *)(path.c_str());
    // path is guaranteed to keep existing this function
    program_invocation_name = c_path;
    program_invocation_short_name = basename(c_path);
    // make sure to have a fresh optind across calls
    // FIXME: fails if run() is executed in parallel
    int old_optind = optind;
    optind = 0;
    int rc = main(argc, argv);
    optind = old_optind;
    if (return_code) {
        *return_code = rc;
    }
    return lib;
}

std::shared_ptr<elf::object> run(string path,
                                 vector<string> args, int* return_code)
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
