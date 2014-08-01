/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define BOOST_TEST_MODULE tst-app

#include <iostream>
#include <thread>
#include <osv/app.hh>
#include <osv/debug.hh>
#include <osv/latch.hh>

using namespace osv;

const char* WAIT_UNTIL_TERMINATION = "wait_until_termination";
const char* RETURN = "return";
const char* prog;

int child(std::string command, int argc, char const *argv[])
{
    auto app = application::get_current();

    if (command == WAIT_UNTIL_TERMINATION) {
        latch termination_requested;

        app->on_termination_request([&] {
            debug("Termination requested.\n");
            termination_requested.count_down();
        });

        debug("Waiting for termination request...\n");
        termination_requested.await();
        return 0;
    }

    if (command == RETURN) {
        return std::atoi(argv[2]);
    }

    std::cerr << "Unknown command: " << command << std::endl;
    return -1;
}

void test_termination_request_before_callback_is_registerred()
{
    debug("%s\n", __FUNCTION__);

    auto app = application::run({prog, WAIT_UNTIL_TERMINATION});
    app->request_termination();
    app->join();
    assert(app->get_return_code() == 0);
}

void test_termination_request_after_callback_is_registerred()
{
    debug("%s\n", __FUNCTION__);

    auto app = application::run({prog, WAIT_UNTIL_TERMINATION});
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    app->request_termination();
    app->join();
    assert(app->get_return_code() == 0);
}

void test_return_code_is_propagated()
{
    debug("%s\n", __FUNCTION__);

    const int code = 123;
    auto app = application::run({prog, RETURN, std::to_string(code)});
    auto result = app->join();
    assert(app->get_return_code() == code);
    assert(result == code);
}

int main(int argc, char const *argv[])
{
    if (argc > 1) {
        return child(argv[1], argc, argv);
    }

    prog = argv[0];
    test_termination_request_before_callback_is_registerred();
    test_termination_request_after_callback_is_registerred();
    test_return_code_is_propagated();
    return 0;
}
