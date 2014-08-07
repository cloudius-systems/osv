/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "cloud-init.hh"
#include <iostream>
#include <fstream>
#include <memory>
#include "client.hh"
#include <boost/asio.hpp>

namespace init {
using namespace std;

class osvinit_exception : public std::exception {
public:
    osvinit_exception(const std::string& msg)
        : msg(msg)
    {

    }
    virtual const char* what() const throw ()
    {
        return msg.c_str();
    }
private:
    std::string msg;
};

static std::string get_url(const std::string& server, const std::string& path,
                       const std::string& port)
{
    client c;
    if (port == "") {
        c.get(server, path);
    } else {
        c.get(server, path, atoi(port.c_str()));
    }

    if (!c.is_ok()) {
        throw std::runtime_error("Request failed: " + c.get_status());
    }

    return c.text();
}

void script_module::load_file(const std::string& path)
{
    if (!mark(path)) {
        handle(YAML::LoadFile(path));
    }
}

void script_module::load_url(const std::string& server, const std::string& path,
                       const std::string& port)
{
    if (!mark(server + path)) {
        load(get_url(server, path, port));
    }
}

void script_module::load(const std::string& script)
{
    handle(YAML::Load(script));
}

void script_module::yaml_to_request(const YAML::Node& node, http::server::request& req)
{
    std::string method;
    for (auto i : node) {
        if (i.first.as<string>() == "GET") {
            method = "GET";
        } else if (i.first.as<string>() == "PUT") {
            method = "PUT";
        } else if (i.first.as<string>() == "POST") {
            method = "POST";
        } else if (i.first.as<string>() == "DELETE") {
            method = "DELETE";
        } else {
            http::server::header param;
            param.name = i.first.as<string>();
            param.value = i.second.as<string>();
            req.query_parameters.push_back(param);
        }
    }
    if (method == "") {
        throw osvinit_exception(
            "Command is missing use GET, PUT, POST or DELETE");
    }
    req.method = method;
    req.uri = node[method].as<string>();
}

void script_module::do_include(http::server::request& api)
{
    if (api.get_query_param("path") != "") {
        load_file(api.get_query_param("path"));
    } else {
        load_url(api.get_query_param("host"), api.get_query_param("url"),
                 api.get_query_param("port"));
    }
}

void script_module::do_api(http::server::request& req)
{
    http::server::reply rep;

    httpserver::global_server::get_routes().handle(req.uri, req, rep);

    if (rep.status != 200) {
        throw osvinit_exception(rep.content);
    }
}

void script_module::handle(const YAML::Node& doc)
{
    for (auto& node : doc) {
        http::server::request req;
        yaml_to_request(node, req);
        if (req.uri == "/include") {
            do_include (req);
        } else if (req.uri == "/open-rest-api") {
            should_wait = true;
            t = std::thread([=] {boost::program_options::variables_map c; httpserver::global_server::run(c); });
        } else if (!req.uri.empty()) {
            do_api(req);
        }
    }
}

void script_module::wait()
{
    if (should_wait) {
        t.join();
    }
}

void osvinit::add_module(std::shared_ptr<config_module> module)
{
    _modules[module->get_label()] = module;
}

void osvinit::load(const std::string& script)
{
    do_yaml(YAML::Load(script));
}

void osvinit::load_file(const std::string& path)
{
    do_yaml(YAML::LoadFile(path));
}

void osvinit::load_url(const std::string& server, const std::string& path,
                       const std::string& port)
{
    load(get_url(server, path, port));
}

/**
 * Wrap the run server command in the global server in a method
 * that can be passed to pthred_create
 */
/*
static void run_server()
{
    httpserver::global_server::run();
}
*/

void osvinit::do_yaml(const YAML::Node& doc)
{
    for (auto& node : doc) {
        std::string label;
        label = node.first.as<std::string>();
        try {
            if (!_modules.count(label)) {
                throw std::runtime_error("Handler not found");
            }
            _modules[label]->handle(node.second);
         } catch (const exception& e) {
            cerr << "cloud-init failed when handling '" << label << "'': " << e.what() << endl;
            if (halt_on_error) {
                return;
            }
        }
    }
}

}
