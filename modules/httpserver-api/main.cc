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
#include <osv/exception_utils.hh>

using namespace httpserver;

namespace po = boost::program_options;

int main(int argc, char* argv[])
{
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("access-allow", po::value<std::string>(),
             "Set the Access-Control-Allow-Origin to *. Note the security risk")
        ("ipaddress", po::value<std::string>(), "set the ip address")
        ("port", po::value<std::string>(), "set the port")
        ("cert", po::value<std::string>(), "path to server's SSL certificate")
        ("key", po::value<std::string>(), "path to server's private key")
        ("cacert", po::value<std::string>(), "path to CA certificate")
        ("ssl", "enable SSL");

    po::variables_map config;
    po::store(po::parse_command_line(argc, argv, desc), config);
    po::notify(config);

    if (config.count("help")) {
        std::cerr << desc << "\n";
        return 1;
    }

    try {
        global_server::run(config);
    } catch (...) {
        std::cerr << "httpserver failed: " << current_what() << std::endl;
        return 1;
    }

    return 0;
}
