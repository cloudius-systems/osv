/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "env.hh"
#include "autogen/env.json.hh"
#include <stdlib.h>
#include <libc.h>

namespace httpserver {

namespace api {

namespace env {

using namespace json;
using namespace std;
using namespace env_json;

extern "C" void httpserver_plugin_register_routes(httpserver::routes* routes) {
    httpserver::api::env::init(*routes);
}

void init(routes& routes)
{
    env_json_init_path("Environment variables API");
    getEnv.set_handler([](const_req req) {
        string param = req.param.at("var").substr(1);
        char* val = getenv(param.c_str());
        if (val == nullptr) {
            throw bad_param_exception("No environment variable " + param);
        }
        return val;
    });
    list_env.set_handler([](const_req req) {
        vector<string> res;

        if (__environ) {
            for (int i=0; __environ[i] ; i++) {
                res.push_back(string(__environ[i]));
            }
        }
        return res;
    });

    setEnv.set_handler([](const_req req) {
        string param = req.param.at("var").substr(1);
        if (setenv(param.c_str(),
                        req.get_query_param("val").c_str(), 1) < 0) {
            throw bad_param_exception("invalid environment variable " + param);
        }
        return "";
    });

    unsetEnv.set_handler([](const_req req) {
            string param = req.param.at("var").substr(1);
            if (unsetenv(param.c_str()) < 0) {
                throw bad_param_exception("Invalid parameter name " + param);
            }
            return "";
        });

}

}
}
}
