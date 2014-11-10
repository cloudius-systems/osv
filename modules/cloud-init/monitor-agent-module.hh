/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef MONITOR_AGENT_MODULE_HH_
#define MONITOR_AGENT_MODULE_HH_

#include "cloud-init.hh"
#include "global_server.hh"
#include "files-module.hh"

namespace init {

class monitor_agent_module : public config_module
{
    virtual void handle(const YAML::Node& doc) override
    {
        YAML::Emitter out;
        out << doc;
        files_module::create_file("/tmp/monitor-agent.conf", out.c_str());
    }

    virtual std::string get_label() override
    {
        return "monitor-agent";
    }
};
}

#endif /* MONITOR_AGENT_MODULE_HH_ */
