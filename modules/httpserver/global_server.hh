/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef GLOBAL_SERVER_HH_
#define GLOBAL_SERVER_HH_
#include "routes.hh"
#include "server.hh"
#include <boost/program_options/variables_map.hpp>
#include <mutex>
#include <condition_variable>

namespace po = boost::program_options;

namespace httpserver {
/**
 * Global server is a singleton class that controls
 * the httpserver
 */
class global_server {
public:
    /**
     * get an instance of the global server
     * @return a reference to the server
     */
    static global_server& get();

    /**
     * get the route object
     * @return a reference to the route object
     */
    static routes& get_routes()
    {
        return get()._routes;
    }

    /**
     * start the main loop of the server
     * The first time the method is called it would loop forever
     * and would never return.
     *
     * @return false if the server already running
     */
    static bool run(po::variables_map& config);

    /**
     * set an entry in the configuration
     * @param key a key to the configuration
     * @param value a value to set
     * @return a reference to the server
     */
    global_server& set(const std::string& key, const std::string& value);

private:

    global_server();
    void set_routes();
    static global_server* instance;
    routes _routes;
    http::server::server* s;
    po::variables_map config;

    /**
     * set configuration based on command line.
     * @param config a variable map
     * @return a reference to the server
     */
    static global_server& set(po::variables_map& config);

};

}

#endif /* GLOBAL_SERVER_HH_ */
