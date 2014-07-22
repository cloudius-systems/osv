/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "cloud-init.hh"
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>

using namespace std;
using namespace init;
namespace po = boost::program_options;

int main(int argc, char* argv[])
{
    try
    {

        po::options_description desc("Allowed options");
        desc.add_options()
        ("help", "produce help message")
        ("skip-error",
         "do not stop on error")
        ("file", po::value<std::string>(),
         "an init file")
        ("server", po::value<std::string>(),
         "a server to read the file from. must come with a --url")
        ("url", po::value<std::string>(),
         "a url at the server")
        ("port", po::value<std::string>()->default_value("80"),
         "a port at the server")
        ;

        po::variables_map config;
        po::store(po::parse_command_line(argc, argv, desc), config);
        po::notify(config);

        if (config.count("help")) {
            std::cerr << desc << "\n";
            return 1;
        }
        osvinit init(config.count("skip-error") > 0);

        if (config.count("file")) {
            init.load_file(config["file"].as<std::string>());
        } else if (config.count("server") > 0 && config.count("url") > 0) {
            init.load_url(config["server"].as<std::string>(),
                config["url"].as<std::string>(),
                config["port"].as<std::string>());
        } else {
            std::cerr << desc << "\n";
            return 1;
        }

        init.wait();
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
