/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ROUTE_INFO_HH_
#define ROUTE_INFO_HH_
#include <string>
#include <functional>

namespace osv {

struct route_info {
    std::string destination;
    std::string gateway;
    std::string flags;
    std::string netif;
    bool ipv6;
};

/**
 * A lambda expression that is used to iterate of the routes.
 * It gets the route_info as a parameter, and return true to continue
 * iterating or false to stop.
 *
 */
typedef std::function<bool(const route_info&)> route_fun;

/**
 * go over all routes, stops if route_func return false
 * @param a function to go over the routes
 * @return 0 on success or errno on failure
 */
int foreach_route(route_fun);
}

#endif /* ROUTE_INFO_HH_ */
