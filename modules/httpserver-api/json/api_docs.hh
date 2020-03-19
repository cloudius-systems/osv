/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef API_DOCS_HH_
#define API_DOCS_HH_
#include "json/json_elements.hh"
#include "routes.hh"
#include <string>

namespace httpserver {

namespace json {

struct api_doc : public json::json_base {
    json::json_element<std::string> path;
    json::json_element<std::string> description;

    void register_params()
    {
        add(&path, "path");
        add(&description, "description");

    }
    api_doc()
    {
        register_params();
    }
    api_doc(const api_doc & e)
    {
        register_params();
        path = e.path;
        description = e.description;
    }
    template<class T>
    api_doc& operator=(const T& e)
    {
        path = e.path;
        description = e.description;
        return *this;
    }
    api_doc& operator=(const api_doc& e)
    {
        path = e.path;
        description = e.description;
        return *this;
    }
};

struct api_docs : public json::json_base {
    json::json_element<std::string> apiVersion;
    json::json_element<std::string> swaggerVersion;
    json::json_list<api_doc> apis;

    void register_params()
    {
        add(&apiVersion, "apiVersion");
        add(&swaggerVersion, "swaggerVersion");
        add(&apis, "apis");

    }
    api_docs()
    {
        apiVersion = "0.0.1";
        swaggerVersion = "1.2";
        register_params();
    }
    api_docs(const api_docs & e)
    {
        apiVersion = "0.0.1";
        swaggerVersion = "1.2";
        register_params();
    }
    template<class T>
    api_docs& operator=(const T& e)
    {
        apis = e.apis;
        return *this;
    }
    api_docs& operator=(const api_docs& e)
    {
        apis = e.apis;
        return *this;
    }
};

#pragma GCC visibility push(default)
void register_api(const std::string& api,
                  const std::string& description);
#pragma GCC visibility pop

/**
 * Initialize the routes object with specific routes mapping
 * @param routes - the routes object to fill
 */
void api_doc_init(routes& routes);

}
}

#endif /* API_DOCS_HH_ */
