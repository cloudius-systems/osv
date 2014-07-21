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
using namespace jvm_json;

static void validate_jvm()
{
    if (!java_api::instance().is_valid()) {
        throw not_found_exception("JVM is not available");
    }
}

class get_jmx_handler : public handler_base {
    void handle(const std::string& path, parameters* parts,
                const http::server::request& req, http::server::reply& rep)
    override
    {
        validate_jvm();
        rep.content.append(
            java_api::instance().get_mbean_info(
                (*parts)["mbean"].substr(1)));
        set_headers(rep, "json");
    }
};

class set_jmx_handler : public handler_base {
    void handle(const std::string& path, parameters* parts,
                const http::server::request& req, http::server::reply& rep)
    override
    {
        if (!java_api::instance().is_valid()) {
            reply400(rep, "JVM is not available");
            return;
        }
        string value = req.get_query_param("value");
        string attr = (*parts)["attribute"].substr(1);
        string mbean = (*parts)["mbean"].substr(1);
        if (value == "" || attr == "" || mbean == "") {
            reply500(rep);
            return;
        }
        java_api::instance().set_mbean_info(mbean, attr, value);
        set_headers(rep, "json");
    }
};

/**
 * Initialize the routes object with specific routes mapping
 * @param routes - the routes object to fill
 */
void init(routes& routes)
{
    jvm_json_init_path();
    getJavaVersion.set_handler([](const_req req)
    {
        validate_jvm();
        return java_api::instance().get_system_property("java.version");
    });

    getJMXvalue.set_handler(new get_jmx_handler());

    setJMXvalue.set_handler(new set_jmx_handler());

    getMbeanList.set_handler([](const_req req)
    {
        validate_jvm();
        return java_api::instance().get_all_mbean();
    });

    getGCinfo.set_handler([](const_req req)
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
        return res;
    });

    forceGC.set_handler([](const_req req)
    {
        validate_jvm();
        java_api::instance().call_gc();
        return "";
    });
}

}
}
}
