/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "cloud-init.hh"
#include "data-source.hh"
#include "files-module.hh"
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <osv/debug.hh>
#include <osv/exception_utils.hh>

using namespace std;
using namespace init;
namespace po = boost::program_options;

static void load_from_cloud(osvinit& init)
{
    auto& ds = get_data_source();
    auto user_data = ds.get_user_data();

    if (user_data.empty()) {
        debug("User data is empty\n");
        return;
    }

    init.load(user_data);
}

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
        auto scripts = make_shared<script_module>();
        init.add_module(scripts);
        init.add_module(make_shared<files_module>());

        if (config.count("file")) {
            init.load_file(config["file"].as<std::string>());
        } else if (config.count("server") > 0 && config.count("url") > 0) {
            init.load_url(config["server"].as<std::string>(),
                config["url"].as<std::string>(),
                config["port"].as<std::string>());
        } else {
            load_from_cloud(init);
        }

        scripts->wait();
    } catch (...) {
        std::cerr << "cloud-init failed: " << what(std::current_exception()) << "\n";
        return 1;
    }

    return 0;
}
