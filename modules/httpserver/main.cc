//
// main.cpp
// ~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "server.hh"

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/asio.hpp>
#include "global_server.hh"

using namespace httpserver;

namespace po = boost::program_options;

int main(int argc, char* argv[])
{
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("access-allow",
             "Set the Access-Control-Allow-Origin to *. Note the security risk")
        ("ipaddress", "set the ip address")
        ("port", "set the port");

    po::variables_map config;
    po::store(po::parse_command_line(argc, argv, desc), config);
    po::notify(config);

    if (config.count("help")) {
        std::cerr << desc << "\n";
        return 1;
    }

    global_server::run(config);

    return 0;
}
