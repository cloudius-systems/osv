/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <osv/debug.hh>
#include <osv/exception_utils.hh>
#include "monitor-agent.hh"

using namespace std;
namespace po = boost::program_options;

int main(int argc, char* argv[])
{
    try {
        po::options_description desc("Allowed options");
        desc.add_options()
        ("help", "produce help message")
        ("bucket", po::value<std::string>()->default_value("osv.stat"),
         "bucket-name")
        ("file",
         po::value<std::string>()->default_value(
             "/tmp/monitor-agent.txt"),
         "local file name to store the sent information")
        ("uuid", po::value<std::string>(),
         "unique identifier, leave empty to create a new one")
        ;

        po::variables_map config;
        po::store(po::parse_command_line(argc, argv, desc), config);
        po::notify(config);

        if (config.count("help")) {
            std::cerr << desc << "\n";
            return 1;
        }
        monitoring_agenet::monitor_agent agent(config);
        agent.run();
    } catch (...) {
        std::cerr << "monitoring-agent failed: "
                  << what(std::current_exception()) << "\n";
        return 1;
    }

    return 0;
}
