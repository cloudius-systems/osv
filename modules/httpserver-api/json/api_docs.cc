/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include "api_docs.hh"
#include "handlers.hh"
#include "transformers.hh"
#include "formatter.hh"

using namespace std;

namespace httpserver {

namespace json {

class api_registry : public handler_base {
    static const string base_path;
    api_docs docs;

    routes& _routes;
public:
    api_registry(routes& _routes) : _routes(_routes)
    {
        _routes.put(GET, base_path, this);
    }
    virtual void handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep) {
        rep.content = formatter::to_json(docs);
        set_headers(rep,"json");
    }

    void reg(const std::string& api, const std::string& description) {
        api_doc doc;
        doc.description = description;
        doc.path = "/" + api;
        docs.apis.push(doc);
        file_handler* index = new file_handler("/usr/mgmt/api/listings/" + api + ".json", new content_replace("json"));
        _routes.put(GET, base_path + "/" + api,index);
    }
};
const string api_registry::base_path = "/api-doc";
static api_registry* registry = nullptr;

#pragma GCC visibility push(default)
void register_api(const std::string& api, const std::string& description) {
    registry->reg(api, description);
}
#pragma GCC visibility pop

void api_doc_init(routes& _routes) {
    registry = new api_registry(_routes);
}

}
}
