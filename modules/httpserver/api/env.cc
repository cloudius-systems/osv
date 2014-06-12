/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "env.hh"
#include "autogen/env.json.hh"
#include <stdlib.h>

namespace httpserver {

namespace api {

namespace env {

using namespace json;
using namespace std;
using namespace env_json;

class get_env : public handler_base {
    bool handle(const std::string& path, parameters* parts,
                const http::server::request& req, http::server::reply& rep)
    override
    {
        string param = (*parts)["var"].substr(1);
        char* val = getenv(param.c_str());
        if (val == nullptr) {
            throw bad_param_exception("No environment variable " + param);
        }
        rep.content = formatter::to_json(val);
        set_headers(rep, "json");
        return true;
    }
};

class set_env : public handler_base {
    bool handle(const std::string& path, parameters* parts,
                const http::server::request& req, http::server::reply& rep)
    override
    {
        string param = (*parts)["var"].substr(1);
        if (setenv(param.c_str(),
                   req.get_query_param("val").c_str(), 1) < 0) {
            throw bad_param_exception("invalid environment variable " + param);
        }
        set_headers(rep, "json");
        return true;
    }
};

void init(routes& routes)
{
    env_json_init_path();
    getEnv.set_handler(new get_env());
    setEnv.set_handler(new set_env());
}

}
}
}
