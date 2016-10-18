/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSVINIT_HH_
#define OSVINIT_HH_
#include <string>
#include <queue>
#include <set>
#include <thread>
#include <memory>
#include "global_server.hh"

#include "yaml-cpp/yaml.h"

namespace init {

class config_module {
public:
    virtual ~config_module() {}

    virtual void handle(const YAML::Node& doc) = 0;
    virtual std::string get_label() = 0;
};

/**
 * osvinit handle initialization from files or remote url.
 */
class osvinit {
public:
    osvinit(bool skip_error, bool force_probe)
        : halt_on_error(!skip_error)
        , _force_probe(force_probe)
    {
    }

    void load(const std::string& script);
    void load_file(const std::string& path);
    void load_url(const std::string& server, const std::string& path,
                  const std::string& port);

    void load_from_cloud(bool ignore_missing_source = false);
    void add_module(std::shared_ptr<config_module> module);
private:
    bool halt_on_error;
    bool _force_probe;
    std::unordered_map<std::string,std::shared_ptr<config_module>> _modules;
private:
    void do_yaml(const YAML::Node& node);
};

class include_module : public init::config_module
{
public:
    virtual void handle(const YAML::Node& doc) override;

    virtual std::string get_label() override
    {
        return "include";
    }

    include_module(osvinit& _init) :
        init(_init) {
    }

private:
    osvinit& init;
    /**
     * load a file and execute it
     * @param path a path to a file on disk
     */
    void load_file(const std::string& path);

    /**
     * load a file from a remote url
     * @param server the server address
     * @param path a path on the server
     */
    void load_url(const YAML::Node& doc);

    /**
     * Check if a file/url was executed
     * and mark it so
     * @param path file/url path
     * @return was it executed before
     */
    bool mark(const std::string& path)
    {
        bool was_marked = executed.find(path) != executed.end();
        executed.insert(path);
        return was_marked;
    }
    std::set<std::string> executed;
};

class script_module : public config_module {
public:
    virtual void handle(const YAML::Node& node) override;

    /**
     * When called the thread would call join to wait for the
     * thread.
     */
    void wait();

    virtual std::string get_label() override
    {
        return "run";
    }
private:

    void do_api(http::server::request& api);
    void yaml_to_request(const YAML::Node& node, http::server::request& req);

private:
    bool should_wait{false};
    std::thread t;
};

class mount_module : public config_module {
public:
    virtual void handle(const YAML::Node& node) override;

    virtual std::string get_label() override
    {
        return "mounts";
    }
private:

    void do_api(http::server::request& api);
    void yaml_to_request(const YAML::Node& node, http::server::request& req);
};

}

#endif /* OSVINIT_HH_ */
