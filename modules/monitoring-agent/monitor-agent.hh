/*
 * monitor-agent.hh
 *
 *  Created on: Nov 4, 2014
 *      Author: amnon
 */

#ifndef MONITOR_AGENT_HH_
#define MONITOR_AGENT_HH_
#include <string>
#include <boost/program_options/variables_map.hpp>

namespace monitoring_agenet {
struct configuration {
    std::string bucket;
    std::string user_id;
    std::string local_file_name;
};


class monitor_agent {
public:
    monitor_agent(const boost::program_options::variables_map& _conf);
    void run();
private:
    configuration config;
};


}




#endif /* MONITOR_AGENT_HH_ */
