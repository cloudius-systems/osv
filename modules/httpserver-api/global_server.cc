/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "global_server.hh"
#include "path_holder.hh"
#include <iostream>
#include <fstream>
#include <dlfcn.h>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include "json/api_docs.hh"
#include "transformers.hh"
#include <osv/options.hh>
#include <osv/osv_c_wrappers.h>
#if !defined(MONITORING)
#include "yaml-cpp/yaml.h"
#else
#include "api/os.hh"
#include "api/fs.hh"
#include "api/file.hh"
#include "api/network.hh"
#include "api/hardware.hh"
#include "api/api.hh"
#include "api/env.hh"
#endif

namespace httpserver {

global_server* global_server::instance = nullptr;

global_server& global_server::get()
{
    if (instance == nullptr) {
        instance = new global_server();
    }
    return *instance;
}

void global_server::termination_handler() {
    get().s->close();
    for( auto plugin : get().plugins) {
        dlclose(plugin);
    }
}

bool global_server::run(std::map<std::string,std::vector<std::string>>& _config)
{
    if (get().s != nullptr) {
        return false;
    }
#if !defined(MONITORING)
    std::string config_file_path = "/tmp/httpserver.conf";
    if (options::option_value_exists(_config, "config-file")) {
        config_file_path = options::extract_option_value(_config, "config-file");
    }
    std::ifstream f(config_file_path);
    if (f.is_open()) {
        try {
            YAML::Node doc = YAML::Load(f);
            for (auto node : doc) {
                auto key = node.first.as<std::string>();
                if (key == "redirects" && node.second.IsSequence()) {
                    get().setup_redirects(node.second);
                }
                else if (key == "file_mappings" && node.second.IsSequence()) {
                    get().setup_file_mappings(node.second);
                }
                else {
                    std::function<std::string(const YAML::Node&)> to_string;

                    to_string = [&](const YAML::Node & n) -> std::string {
                        std::string s;
                        if (n.IsSequence()) {
                            for (auto & sn : n) {
                                if (!s.empty()) {
                                    s += ',';
                                }
                                s += to_string(sn);
                            }
                        } else {
                            s = n.as<std::string>();
                        }
                        return s;
                    };

                    auto val = to_string(node.second);
                    get().set(key, val);
                }
            }
        } catch (const std::exception& e) {
            std::cout << "httpserver Failed reading the configuration file " << e.what() <<  std::endl;
            throw e;
        }
    }
#endif

    set(_config);
    get().set("ipaddress", "0.0.0.0");
    get().set("port", "8000");

#if !defined(MONITORING)
    if (get().config.count("ssl")) {
        get().set("cert", "/etc/pki/server.pem");
        get().set("key", "/etc/pki/private/server.key");
        get().set("cacert", "/etc/pki/CA/cacert.pem");
    }
#endif

    auto port = get().config["port"][0];
    get().s = new http::server::server(get().config, &get()._routes);

    osv_current_app_on_termination_request(termination_handler);

    std::cout << "Rest API server running on port " << port << std::endl;
    get().s->run();
    return true;
}

#if !defined(MONITORING)
void global_server::setup_file_mappings(const YAML::Node& file_mappings_node) {
    auto debug_enabled = osv_debug_enabled();
    for (auto node : file_mappings_node) {
        const YAML::Node path = node["path"];
        if (path && node["directory"]) {
            const std::string directory = node["directory"].as<std::string>();
            const YAML::Node content_replace_node = node["content_replace"];
            directory_handler* handler = content_replace_node ?
                                         new directory_handler(directory, new content_replace(content_replace_node.as<std::string>())) :
                                         new directory_handler(directory);
            _routes.add(GET, url(path.as<std::string>()).remainder("path"), handler);
            if (debug_enabled) {
                std::cout << "httpserver: setup directory mapping: [" << path.as<std::string>() << "] -> [" << directory << "]" << std::endl;
            }
        }
        else if (path && node["file"]) {
            const std::string file = node["file"].as<std::string>();
            const YAML::Node exact_match_node = node["exact_match"];
            file_handler* handler = new file_handler(file, nullptr, true);
            if (exact_match_node && exact_match_node.as<bool>()) {
                _routes.put(GET, path.as<std::string>(), handler);
            }
            else {
                _routes.add(GET, url(path.as<std::string>()).remainder("path"), handler);
            }
            if (debug_enabled) {
                std::cout << "httpserver: setup file mapping: [" << path.as<std::string>() << "] -> [" << file << "]" << std::endl;
            }
        }
    }
}

void global_server::setup_redirects(const YAML::Node& redirects_node) {
    for (auto node : redirects_node) {
        const YAML::Node path_node = node["path"];
        const YAML::Node target_path_node = node["target_path"];
        if (path_node && target_path_node) {
            const std::string path = path_node.as<std::string>();
            const std::string target_path = target_path_node.as<std::string>();

            function_handler* redirect =
                    new function_handler(
                            [target_path](const_req req) {
                                throw redirect_exception(req.get_protocol_name() + "://" +
                                                         req.get_header("Host") + target_path);
                                // The return is required so the lambda expression would have
                                // the right signature
                                return "";
                            });
            _routes.put(GET, path, redirect);
            if (osv_debug_enabled()) {
                std::cout << "httpserver: setup redirect: [" << path << "] -> [" << target_path << "]" << std::endl;
            }
        }
    }
}
#endif

global_server::global_server()
    : s(nullptr)
{
    set_routes();
}

global_server& global_server::set(std::map<std::string,std::vector<std::string>> &_config)
{
    for (auto i : _config) {
        get().config.insert(std::make_pair(i.first, i.second));
    }
    return *instance;
}

global_server& global_server::set(const std::string& key,
                                  const std::string& _value)
{
    std::vector<std::string> values;
    values.push_back(_value);
    config.insert(std::make_pair(std::string(key), values));
    return *this;
}

void global_server::set_routes()
{
    path_holder::set_routes(&_routes);
    json::api_doc_init(_routes);
#if defined(MONITORING)
    httpserver::api::api::init(_routes);
    httpserver::api::fs::init(_routes);
    httpserver::api::os::init(_routes);
    httpserver::api::network::init(_routes);
    httpserver::api::hardware::init(_routes);
    httpserver::api::env::init(_routes);
    httpserver::api::file::init(_routes);
#endif
    {
        namespace fs = boost::filesystem;
        fs::path plugin_path("/usr/mgmt/plugins/");
        if (!fs::exists(plugin_path) && !fs::is_directory(plugin_path)) return;
        BOOST_FOREACH(const fs::path& path, std::make_pair(fs::directory_iterator(plugin_path),
                                               fs::directory_iterator())) {
            if (fs::extension(path)==".so") {
                load_plugin(path.string());
            }
        }
    }
}

void global_server::load_plugin(const std::string& path)
{
    void* plugin = dlopen(path.c_str(), RTLD_LAZY);
    if ( plugin == nullptr ) {
        return;
    }
    //
    // The httpserver plugin needs to export proper initialization function intended to register
    // any new routes. The function should be named "httpserver_plugin_register_routes" and
    // defined to follow signature like so:
    //   extern "C" void httpserver_plugin_register_routes(httpserver::routes* routes) {
    //      ....
    //   }
    // This is necessary to avoid C++ symbol mangling so that dlsym() can find the symbol
    using init_func_t = void(routes*);
    init_func_t* httpserver_plugin_register_routes = reinterpret_cast<init_func_t*>(dlsym(plugin, "httpserver_plugin_register_routes"));
    if ( httpserver_plugin_register_routes == nullptr ) {
        dlclose(plugin);
        return;
    }
    plugins.push_back(plugin);
    httpserver_plugin_register_routes(&_routes);
    if (osv_debug_enabled()) {
        std::cout << "httpserver: loaded plugin from path: " << path << std::endl;
    }
}
}
