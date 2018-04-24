/*
 * Copyright (C) 2018 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include <osv/app.hh>

// This tiny app acts as a front-end to Golang applications running on OSv.
// In essence it starts new application to execute specified Golang shared
// object. The spawn application is setup to terminate all remaining Golang
// runtime threads after main thread exits.

using namespace osv;

// As described in https://github.com/golang/go/issues/11100 some
// Golang runtime threads do not terminate automatically when
// main thread exits. This is very similar to JVM shutdown
// behavior.
// The stop_all_remaining_golang_threads() function stops
// all threads associated with the new Golang app started and
// gets passed as post-main function below.
// This call is unsafe, in the sense we assume that those
// renegade threads are not holding any critical resources
// (e.g., not in the middle of I/O or memory allocation).
static void stop_all_remaining_golang_threads()
{
    while(!application::unsafe_stop_and_abandon_other_threads()) {
        usleep(100000);
    }
}

extern "C"
int main(int argc, char **argv)
{
    const std::string go_program(argv[1]);

    std::vector<std::string> go_program_arguments;
    for(auto i = 2; i < argc; i++ )
        go_program_arguments.push_back(argv[i]);
    //
    // Setup new app with Golang specific main function name - GoMain - and
    // post main function that will terminate all remaining Golang runtime threads
    auto app = application::run(go_program, go_program_arguments, false, nullptr,
                                "GoMain", stop_all_remaining_golang_threads);
    return app->join();
}
