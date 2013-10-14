#include <string.h>

#include <elf.hh>
#include <osv/run.hh>

#include <debug.hh>

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

}
