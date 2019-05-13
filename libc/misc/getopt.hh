/*
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef GETOPT_HH_
#define GETOPT_HH_

#include <osv/app.hh>

// As explained in http://www.shrubbery.net/solaris9ab/SUNWdev/LLM/p22.html#CHAPTER4-84604
// newer versions of gcc produce position independent executables with copies of
// some global variables like those used by getopt() and getopt_long() for optimizations reason.
// In those circumstances the caller of these functions uses different copies of
// global variables (like optind) than the getopt() code that is part of OSv kernel.
// For that reason in the beginning of these functions we need to copy values of the caller
// copies of those variables to the kernel placeholders. Likewise on every return from the function
// we need to copy the values of kernel copies of global variables to the caller ones.
//
// See http://man7.org/linux/man-pages/man3/getopt.3.html
//
// This is a simple RAII class for retrieving the caller's copy of the global opt* variables
// on initialization, and returning them back to the caller on destruction.
class getopt_caller_vars_copier {
    std::shared_ptr<osv::application_runtime> _runtime;
    int *other_optind;

public:
    getopt_caller_vars_copier() : _runtime(sched::thread::current()->app_runtime()) {
        if (_runtime) {
            auto obj = _runtime->app.lib();
            other_optind = reinterpret_cast<int*>(obj->cached_lookup("optind"));
            if (other_optind) {
                optind = *other_optind;
            }

            auto other_opterr = reinterpret_cast<int*>(obj->cached_lookup("opterr"));
            if (other_opterr) {
                opterr = *other_opterr;
            }
        }
    }

    ~getopt_caller_vars_copier() {
        if (_runtime) {
            auto obj = _runtime->app.lib();
            if (other_optind) {
                *other_optind = optind;
            }
            auto other_optopt = reinterpret_cast<int*>(obj->cached_lookup("optopt"));
            if (other_optopt) {
                *other_optopt = optopt;
            }
            auto other_optarg = reinterpret_cast<char**>(obj->cached_lookup("optarg"));
            if (other_optarg) {
                *other_optarg = optarg;
            }
        }
    }
};

extern "C" int __getopt(int argc, char * const argv[], const char *optstring);

#endif
