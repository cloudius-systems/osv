/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include "network.hh"
#include "autogen/network.json.hh"
#include "tools/route/route_info.hh"
#include "exception.hh"
#include <vector>

namespace httpserver {

namespace api {

namespace network {
using namespace json;
using namespace std;
using namespace httpserver::json;

/**
 * Initialize the routes object with specific routes mapping
 * @param routes - the routes object to fill
 */
void init(routes& routes)
{
    network_json_init_path();
    network_json::getRoute.set_handler([](const_req req) {
        vector<Route> res;
        osv::foreach_route([&res](const osv::route_info& route){
            res.push_back(Route());
            res.back().set(route);
            return true;
        });
        return res;
    });
}

}
}
}
