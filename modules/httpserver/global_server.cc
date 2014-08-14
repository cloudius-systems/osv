/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "global_server.hh"
#include "api/os.hh"
#include "api/fs.hh"
#include "api/files_mapping.hh"
#include "api/jvm.hh"
#include "api/file.hh"
#include "api/trace.hh"
#include "api/env.hh"
#include "api/hardware.hh"
#include "path_holder.hh"
#include "api/network.hh"
#include <iostream>
#include <osv/app.hh>
#include <fstream>
#include "yaml-cpp/yaml.h"

namespace httpserver {

global_server* global_server::instance = nullptr;

global_server& global_server::get()
{
    if (instance == nullptr) {
        instance = new global_server();
    }
    return *instance;
}

bool global_server::run(po::variables_map& _config)
{
    if (get().s != nullptr) {
        return false;
    }
    std::ifstream f("/tmp/httpserver.conf");
    if (f.is_open()) {
        try {
            YAML::Node doc = YAML::Load(f);
            for (auto node : doc) {
                get().set(node.first.as<std::string>(), node.second.as<std::string>());
            }
        } catch (const std::exception& e) {
            std::cout << "httpserver Failed reading the configuration file " << e.what() <<  std::endl;
            throw e;
        }
    }

    set(_config);
    get().set("ipaddress", "0.0.0.0");
    get().set("port", "8000");
    get().set("cert", "/etc/pki/server.pem");
    get().set("key", "/etc/pki/private/server.key");
    get().set("cacert", "/etc/pki/CA/cacert.pem");

    get().s = new http::server::server(&get().config, &get()._routes);

    osv::this_application::on_termination_request([&] {
        get().s->close();
    });

    get().s->run();
    return true;
}


global_server::global_server()
    : s(nullptr)
{
    set_routes();

}

global_server& global_server::set(po::variables_map& _config)
{
    for (auto i : _config) {
        get().config.insert(std::make_pair(i.first, i.second));
    }
    return *instance;
}

global_server& global_server::set(const std::string& key,
                                  const std::string& _value)
{
    boost::any val(_value);
    boost::program_options::variable_value v(val, false);
    config.insert(std::make_pair(std::string(key), v));
    return *this;

}

void global_server::set_routes()
{
    path_holder::set_routes(&_routes);
    api::network::init(_routes);
    api::os::init(_routes);
    api::fs::init(_routes);
    api::file::init(_routes);
    api::jvm::init(_routes);
    api::trace::init(_routes);
    api::env::init(_routes);
    api::files_mapping::init(_routes);
    api::hardware::init(_routes);
}

}
