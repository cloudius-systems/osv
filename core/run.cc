#include <string.h>
#include <algorithm>
#include <vector>
#include <memory>
#include <boost/range/algorithm/transform.hpp>

#include <osv/elf.hh>
#include <osv/run.hh>

#include <osv/debug.hh>
#include <errno.h>
#include <libgen.h>

extern int optind;

using namespace std;
using namespace boost::range;

// We will handle all the initialization ourselves with the osv::run() API.
// Still, if objects are linked with the -z now flag, they may possibly
// require this symbol to run. The symbol, though, should never be reached.
extern "C" void __libc_start_main()
{
    abort("Invalid call to __libc_start_main");
}

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

    auto sz = argc; // for the trailing 0's.
    for (int i = 0; i < argc; ++i) {
        sz += strlen(argv[i]);
    }

    std::unique_ptr<char []> argv_buf(new char[sz]);
    char *ab = argv_buf.get();
    char *contig_argv[argc + 1];

    for (int i = 0; i < argc; ++i) {
        size_t asize = strlen(argv[i]);
        memcpy(ab, argv[i], asize);
        ab[asize] = '\0';
        contig_argv[i] = ab;
        ab += asize + 1;
    }
    contig_argv[argc] = nullptr;

    // make sure to have a fresh optind across calls
    // FIXME: fails if run() is executed in parallel
    int old_optind = optind;
    optind = 0;
    int rc = main(argc, contig_argv);
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
