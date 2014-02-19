/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "jvm.hh"
#include "autogen/jvm.json.hh"
#include "java_api.hh"
#include <string>

namespace httpserver {

namespace api {

namespace jvm {

using namespace json;
using namespace std;

static void validate_jvm()
{
    if (!java_api::instance().is_valid()) {
        throw not_found_exception("JVM is not available");
    }
}

class get_jmx_handler : public handler_base {
    bool handle(const std::string& path, parameters* parts,
            const http::server::request& req, http::server::reply& rep)
    override
    {
        validate_jvm();
        rep.content.append(
                java_api::instance().get_mbean_info(
                        (*parts)["mbean"].substr(1)));
        set_headers(rep, "json");
        return true;
    }
};

class set_jmx_handler : public handler_base {
    bool handle(const std::string& path, parameters* parts,
            const http::server::request& req, http::server::reply& rep)
    override
    {
        if (!java_api::instance().is_valid()) {
            reply400(rep, http::server::reply::status_type::not_found,
                    "JVM is not available");
            return false;
        }
        string value = req.get_query_param("value");
        string attr = (*parts)["attribute"].substr(1);
        string mbean = (*parts)["mbean"].substr(1);
        if (value == "" || attr == "" || mbean == "") {
            reply500(rep);
            return false;
        }
        java_api::instance().set_mbean_info(mbean, attr, value);
        set_headers(rep, "json");
        return true;
    }
};

/**
 * Initialize the routes object with specific routes mapping
 * @param routes - the routes object to fill
 */
void init(routes& routes)
{
    jvm_json_init_path();
    function_handler* java_version =
            new function_handler(
                    [](const_req req)
                    {
                        validate_jvm();
                        return formatter::to_json(java_api::instance().get_system_property("java.version") );
                    }, "json");
    routes.add_path("getJavaVersion", java_version);

    routes.add_path("getJMXvalue", new get_jmx_handler());

    routes.add_path("setJMXvalue", new set_jmx_handler());

    function_handler* mbean_list =
            new function_handler(
                    [](const_req req)
                    {
                        validate_jvm();
                        return formatter::to_json(java_api::instance().get_all_mbean() );
                    }, "json");
    routes.add_path("getMbeanList", mbean_list);

    function_handler* gc_info_handler =
            new function_handler(
                    [](const_req req)
                    {
                        validate_jvm();
                        vector<MemoryManager> res;
                        std::vector<gc_info> gc_collection = java_api::instance().get_all_gc();
                        for (gc_info gc: gc_collection) {
                            res.push_back(MemoryManager());
                            res.back().count = gc.count;
                            res.back().time = gc.time;
                            res.back().name = gc.name;

                        }
                        return formatter::to_json(res);
                    }, "json");
    routes.add_path("getGCinfo", gc_info_handler);

    function_handler* gc_handler =
            new function_handler(
                    [](const_req req)
                    {
                        validate_jvm();
                        java_api::instance().call_gc();
                        return "";
                    }, "json");
    routes.add_path("forceGC", gc_handler);
}

}
}
}
