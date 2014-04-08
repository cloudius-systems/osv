/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "file.hh"
#include "routes.hh"
#include "transformers.hh"
#include "autogen/file.json.hh"
#include <iostream>
#include <fstream>
#include <sys/stat.h>

namespace httpserver {

namespace api {

namespace file {

using namespace json;
using namespace std;
using namespace file_json;

/**
 * A helper function to set the op and path param
 * It validate that both exists and if not, throw an exception
 * @param params the parameters object
 * @param req the request
 * @param op will hold the op parameter
 * @param path will hold the path parameter
 */
static void set_and_validate_params(parameters* params,
                                    const http::server::request& req, string& op, string& path)
{
    op = req.get_query_param("op");
    path = (*params)["path"];
    if (op == "" || path == "") {
        throw bad_param_exception("missing mandatory parameters");
    }
}

class get_file_handler : public file_interaction_handler {
    virtual bool handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        string op, full_path;
        set_and_validate_params(params, req, op, full_path);
        if (op == "OPEN") {
            return read(full_path, req, rep);
        }
        return true;
    }
};

class del_file_handler : public handler_base {
    virtual bool handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        string op, full_path;
        set_and_validate_params(params, req, op, full_path);
        if (op == "DELETE") {
            remove(full_path.c_str());
        }
        return true;
    }
};

class post_file_handler : public handler_base {
    virtual bool handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        string full_path = (*params)["path"];
        string from = req.get_header("file_name");
        std::ifstream src(req.get_header("file_name"), std::ios::binary);
        std::ofstream dst(full_path,
                          std::ios::binary);
        if (!dst) {
            std::cout<< strerror(errno) << std::endl;
            throw not_found_exception("Fail opening file '" + full_path + "'");
        }
        dst << src.rdbuf();
        set_headers(rep, "json");
        return true;
    }
};

void init(routes& routes)
{
    file_json_init_path();

    routes.add_path(getFile, new get_file_handler());
    routes.add_path(delFile, new del_file_handler());
    routes.add_path(upload, new post_file_handler());
}

}
}
}
