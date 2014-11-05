/*
 * monitor-agent.cc
 *
 *  Created on: Nov 4, 2014
 *      Author: amnon
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
#include <osv/sched.hh>
#include <osv/clock.hh>
#include "java_api.hh"
#include <osv/debug.hh>

using namespace std;

namespace monitoring_agenet {

monitor_agent::monitor_agent(
        const boost::program_options::variables_map& _conf)
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
    data_container& operator()(const string& key, const string& value)
    {
        data << key << ":" << value << endl;
        return *this;
    }
    string str() const
    {
        return data.str();
    }
private:
    stringstream data;
};

static void fill_data(data_container& data, const string& uuid)
{
    using namespace std::chrono;
    struct sysinfo info;
    sysinfo(&info);
    data("uuid", uuid)
    ("/os/name", "OSv")
    ("/os/version", osv::version())
    ("/os/date", to_string(duration_cast<milliseconds>
            (osv::clock::wall::now().time_since_epoch()).count()))
    ("/os/memory/total", to_string(info.totalram))
    ("/os/memory/free", to_string(info.freeram))
    ("/os/cmdline", osv::getcmdline())
    ("/hardware/hypervisor", osv::firmware_vendor())
    ("/hardware/processor/count", to_string(sched::cpus.size()));
    if (java_api::instance().is_valid()) {
        data("/jvm/version",
                java_api::instance().get_system_property("java.version"));
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

void monitor_agent::run()
{
    client c;
    string s = (config.user_id == "") ? get_uuid() : config.user_id;

    data_container data;
    fill_data(data, s);
    c.upload(config.bucket + ".s3.amazonaws.com", "/", 80, s + ".txt", "",
            data.str());
    store_local(data, config.local_file_name);
    debug("This is a beta build of OSv. This version will send a report "
                    "each time it is booted to Cloudius Systems.  For more on "
                    "information collected, how and why it is stored, and how to "
                    "disable reporting, see osv.io/osv-stat\n");
}

}
