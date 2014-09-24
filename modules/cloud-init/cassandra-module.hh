/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CLOUDINIT_CASSANDRA_HH_
#define CLOUDINIT_CASSANDRA_HH_

#include "cloud-init.hh"
#include "json.hh"
#include <map>

namespace init {

class cassandra_module : public init::config_module {
public:
    virtual void handle(const YAML::Node& doc) override;

    virtual std::string get_label() {
        return "cassandra";
    }
private:
    std::string reflector_seeds(std::map<std::string, std::string> dict);
    json11::Json wait_for_seeds(std::map<std::string, std::string> dict);

private:
    std::map<std::string, std::string> to_map(const YAML::Node& doc) const;
};

}

#endif
