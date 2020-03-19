/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include "network.hh"
#include "autogen/network.json.hh"
#include "../libtools/route_info.hh"
#include "../libtools/network_interface.hh"
#include "exception.hh"
#include <vector>
#include <osv/clock.hh>

namespace httpserver {

namespace api {

namespace network {
using namespace json;
using namespace std;
using namespace httpserver::json;
using namespace osv::network;
using namespace std::chrono;

static Interface get_interface(const string& name, ifnet* ifp, long time)
{
    Interface_config ifc;
    Interface_data ifd;
    Interface f;
    interface intf(name);

    if_data cur_data = { 0 };
    if (!set_interface_info(ifp, cur_data, intf)) {
        throw server_error_exception("Failed getting interface information");
    }
    ifc = intf;
    f.config = ifc;
    ifd = cur_data;
    f.data = ifd;
    f.time = time;
    return f;
}

#if !defined(MONITORING)
extern "C" void httpserver_plugin_register_routes(httpserver::routes* routes) {
    httpserver::api::network::init(*routes);
}
#endif

/**
 * Initialize the routes object with specific routes mapping
 * @param routes - the routes object to fill
 */
void init(routes& routes)
{
    network_json_init_path("Hardware management API");
    network_json::listIfconfig.set_handler([](const_req req) {
        vector<Interface> res;
        auto time = duration_cast<microseconds>
                    (osv::clock::uptime::now().time_since_epoch()).count();
        for (unsigned int i = 0; i <= number_of_interfaces(); i++) {
            auto* ifp = get_interface_by_index(i);

            if (ifp != nullptr) {
                res.push_back(get_interface(get_interface_name(ifp), ifp, time));
            }
        }
        return res;
    });

    network_json::getIfconfig.set_handler([](const_req req) {
        string name = req.param.at("intf").substr(1);
        interface intf(name);
        ifnet* ifp = get_interface_by_name(name);
        if (ifp == nullptr) {
            throw not_found_exception(string("Interface ") + name + " not found");
        }
        auto time = duration_cast<microseconds>(osv::clock::uptime::now().time_since_epoch()).count();
        return get_interface(name, ifp, time);
    });

    network_json::getRoute.set_handler([](const_req req) {
        vector<Route> res;
        osv::foreach_route([&res](const osv::route_info& route) {
            res.push_back(Route());
            res.back() = route;
            return true;
        });
        return res;
    });
}

}
}
}
