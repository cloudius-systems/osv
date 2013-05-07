#include <algorithm>
#include "elf.hh"
#include <jni.h>
#include <string.h>

extern elf::program* prog;

const int argc_max_arguments = 256;

bool run_elf(int argc, char** argv, int *return_code)
{
    if ((argc <= 0) || (argc > argc_max_arguments)) {
        return (false);
    }

    auto obj = prog->add_object(argv[0]);
    if (!obj) {
        return (false);
    }

    auto main = obj->lookup<int (int, char**)>("main");
    if (!main) {
       return (false);
    }

    /* call main in a thread */
    int rc = main(argc, argv);

    /* cleanups */
    prog->remove_object(argv[0]);

    /* set the return code */
    if (return_code) {
        *return_code = rc;
    }

    return (true);
}

