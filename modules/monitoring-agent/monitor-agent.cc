/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "monitor-agent.hh"

#include "client.hh"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>
#include <sstream>
#include "osv/version.hh"
#include <osv/commands.hh>
#include <sys/sysinfo.h>
#include <fstream>
#include <osv/firmware.hh>
#include <osv/hypervisor.hh>
#include <osv/sched.hh>
#include <osv/clock.hh>
#include "java_api.hh"
#include <osv/debug.hh>
#include "yaml-cpp/yaml.h"

extern char debug_buffer[DEBUG_BUFFER_SIZE];

using namespace std;

namespace monitoring_agent {

monitor_agent::monitor_agent(const boost::program_options::variables_map& _conf)
{
    config.bucket = _conf["bucket"].as<std::string>();
    config.local_file_name = _conf["file"].as<std::string>();
    if (_conf.count("id")) {
        config.user_id = _conf["id"].as<std::string>();
    }
}

static string get_uuid()
{
    boost::uuids::uuid u = boost::uuids::random_generator()();
    return boost::lexical_cast<string>(u);
}

class data_container {
public:
    data_container& operator()(const string& key, const string& value) {
        data << key << ":" << value << endl;
        return *this;
    }
    string str() const {
        return data.str();
    }
private:
    stringstream data;
};

static void fill_data(data_container& data, const string& uuid)
{
    using namespace std::chrono;
    struct sysinfo info;

    std::string debug_buffer_str(debug_buffer);
    std::replace( debug_buffer_str.begin(), debug_buffer_str.end(), '\n', ';');

    sysinfo(&info);
    data("uuid", uuid)
        ("/os/name", "OSv")
        ("/os/version", osv::version())
        ("/os/date", to_string(duration_cast<milliseconds>(osv::clock::wall::now().time_since_epoch()).count()))
        ("/os/memory/total", to_string(info.totalram))
        ("/os/memory/free", to_string(info.freeram))
        ("/os/cmdline", osv::getcmdline())
        ("/os/dmesg", debug_buffer_str)
        ("/hardware/hypervisor", osv::hypervisor_name())
        ("/hardware/processor/count", to_string(sched::cpus.size()));
    if (java_api::instance().is_valid()) {
        data("/jvm/version", java_api::instance().get_system_property("java.version"));
    }
}

static void store_local(data_container& data, const string& file_name)
{
    if (file_name == "") {
        return;
    }
    ofstream f(file_name);
    f << data.str();
}

void monitor_agent::load_config() {
    std::ifstream f("/tmp/monitor-agent.conf");
    if (f) {
        try {
            YAML::Node conf_yaml = YAML::Load(f);
            if (conf_yaml["enable"].as<std::string>() == "false") {
               config.enable = false;
            }
        } catch (const std::exception& e) {
            std::cout << "monitoring agent Failed reading the configuration file " << e.what() <<  std::endl;
            throw e;
        }
    }
}


void monitor_agent::run()
{
    load_config();

    if (!config.enable) {
       return;
    }

    client c;
    string s = (config.user_id == "") ? get_uuid() : config.user_id;

    data_container data;
    fill_data(data, s);

    c.upload(config.bucket + ".s3.amazonaws.com", "/", 80, s + ".txt", "", data.str());

    store_local(data, config.local_file_name);
    debug("This version of OSv will send a report "
          "each time it is booted to Cloudius Systems.  For more on "
          "information collected, how and why it is stored, and how to "
          "disable reporting, see osv.io/osv-stat\n");
}

}
