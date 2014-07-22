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
#include "global_server.hh"

#include "yaml-cpp/yaml.h"

namespace init {
/**
 * osvinit handle initialization from files or remote url.
 */
class osvinit {
public:
    osvinit(bool skip_error)
        : halt_on_error(!skip_error), should_wait(false)
    {
    }

    void do_yaml(const YAML::Node& doc);

    /**
     * When called the thread would call join to wait for the
     * thread.
     */
    void wait();

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
    void load_url(const std::string& server, const std::string& path,
                  const std::string& port);

private:
    void do_api(http::server::request& api);
    void do_include(http::server::request& api);
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

    bool halt_on_error;

    bool should_wait;

    std::thread t;
};

}

#endif /* OSVINIT_HH_ */
