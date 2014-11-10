/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef MONITOR_AGENT_HH_
#define MONITOR_AGENT_HH_

#include <string>
#include <boost/program_options/variables_map.hpp>

namespace monitoring_agent {

class configuration {
public:
    configuration() : enable(true) {};
    std::string bucket;
    std::string user_id;
    std::string local_file_name;
    bool enable;
};

class monitor_agent {
public:
    monitor_agent(const boost::program_options::variables_map& _conf);

    void run();
private:
    void load_config();
    configuration config;
};

}

#endif /* MONITOR_AGENT_HH_ */
