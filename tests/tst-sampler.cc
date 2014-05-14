/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/sampler.hh>
#include <chrono>
#include <thread>
#include <iostream>

int main(int argc, char const *argv[])
{
    prof::config _config = { std::chrono::milliseconds(1) };

    std::cout << "Starting" << std::endl;
    prof::start_sampler(_config);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::cout << "Stopping" << std::endl;
    prof::stop_sampler();

    std::cout << "Starting" << std::endl;
    prof::start_sampler(_config);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::cout << "Stopping" << std::endl;
    prof::stop_sampler();

    std::cout << "Done" << std::endl;
    return 0;
}
