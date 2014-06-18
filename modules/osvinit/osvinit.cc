/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "osvinit.hh"
#include <iostream>
#include <fstream>
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

void osvinit::load_file(const std::string& path, bool once)
{
    if (mark(path) && once) {
        return;
    }
    YAML::Node config = YAML::LoadFile(path);
    do_yaml(config);
}

void osvinit::load_url(const std::string& server, const std::string& path,
                       const std::string& port,
                       bool once)
{
    if (mark(server + path) && once) {
        return;
    }
    client c;
    if (port == "") {
        c.get(server, path);
    } else {
        c.get(server, path, atoi(port.c_str()));
    }

    boost::asio::streambuf buf;
    std::iostream os(&buf);
    os  << c;

    YAML::Node config = YAML::Load(os);
    do_yaml(config);
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

void osvinit::wait()
{
    if (should_wait) {
        t.join();
    }
}

static void yaml_to_request(const YAML::Node& node, http::server::request& req)
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

void osvinit::do_yaml(const YAML::Node& doc)
{
    for (YAML::const_iterator it = doc.begin(); it != doc.end(); ++it) {
        try {
            http::server::request req;
            yaml_to_request(*it, req);
            if (req.uri == "/include") {
                do_include (req);
            } else if (req.uri == "/open-rest-api") {
                should_wait = true;
                t = std::thread([=] { httpserver::global_server::run(); });
            } else if (!req.uri.empty()) {
                do_api(req);
            }
        } catch (const exception& e) {
            cerr << "osvinit failed with error: " << e.what() << endl;
            if (halt_on_error) {
                return;
            }
        }

    }
}

void osvinit::do_include(http::server::request& api)
{
    bool once = api.get_query_param("once")  == "True";
    if (api.get_query_param("path") != "") {
        load_file(api.get_query_param("path"), once);
    } else {
        load_url(api.get_query_param("host"), api.get_query_param("url"),
                 api.get_query_param("port"), once);
    }
}

void osvinit::do_api(http::server::request& req)
{
    http::server::reply rep;

    if (!httpserver::global_server::get_routes().handle(req.uri, req, rep) ||
            rep.status != 200) {
        throw osvinit_exception(rep.content);
    }
}

}
