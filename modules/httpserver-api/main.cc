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

#include <boost/asio.hpp>
#include "global_server.hh"
#include <osv/exception_utils.hh>
#include <osv/options.hh>

using namespace httpserver;

static void usage()
{
    std::cout << "Allowed options:\n";
    std::cout << "  --help                                produce help message\n";
    std::cout << "  --config-file arg (=/tmp/httpserver.conf)\n";
    std::cout << "                                        configuration file path\n";
    std::cout << "  --access-allow arg                    Set the Access-Control-Allow-Origin to\n";
    std::cout << "                                        *. Note the security risk\n";
    std::cout << "  --ipaddress arg                       set the ip address\n";
    std::cout << "  --port arg                            set the port\n";
    std::cout << "  --cert arg                            path to server's SSL certificate\n";
    std::cout << "  --key arg                             path to server's private key\n";
    std::cout << "  --cacert arg                          path to CA certificate\n";
    std::cout << "  --ssl                                 enable SSL\n\n";
}

static void handle_parse_error(const std::string &message)
{
    std::cout << message << std::endl;
    usage();
    exit(1);
}

int __attribute__((visibility("default"))) main(int argc, char* argv[])
{
    auto options_values = options::parse_options_values(argc - 1, argv + 1, handle_parse_error);

    if (options::extract_option_flag(options_values, "help", handle_parse_error)) {
        usage();
        return 1;
    }

    try {
        global_server::run(options_values);
    } catch (...) {
        std::cerr << "httpserver failed: " << current_what() << std::endl;
        return 1;
    }

    return 0;
}
