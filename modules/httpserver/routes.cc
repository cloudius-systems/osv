/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "routes.hh"
#include "reply.hh"

namespace httpserver {

using namespace std;

routes::~routes()
{
	for (int i = 0; i < 2; i++) {
		for (auto kv : map[i]) {
			delete kv.second;
		}
	}
	for (auto r : rules) {
		delete r;
	}

}

bool routes::handle(const string& path, const http::server::request& req,
                    http::server::reply& rep)
{
    parameters params;
    string paramStr;

    handler_base* handler = get_handler(str2type(req.method),
                                        normalize_url(path, paramStr), params);
    if (handler != nullptr) {
        try {
            handler->handle(path, &params, req, rep);
        } catch (exception& e) {
            cerr << "exception was caught for " << path << ": " << e.what() << endl;
            handler->reply500(rep, 500, e.what());
            return false;
        }
    } else {
        rep = http::server::reply::stock_reply(http::server::reply::not_found);
        return false;
    }
    return true;
}

std::string routes::normalize_url(const std::string& url,
                                  std::string& paramPart)
{
    int param = url.find('?');
    if (param > 0) {
        paramPart = url.substr(param, url.length() - param - 1);
        return (url.at(param - 1) == '/') ?
               url.substr(0, param - 2) :
               url.substr(0, param - 1);
    }
    return (url.length() < 2 || url.at(url.length() - 1) != '/') ?
           url : url.substr(0, url.length() - 1);
}

handler_base* routes::get_handler(operation_type type, const string& url,
                                  parameters& params)
{
    handler_base* handler = get_exact_match(type, url);
    if (handler != nullptr) {
        return handler;
    }
    for (auto rule = rules.cbegin(); rule != rules.cend(); ++rule) {
        handler = (*rule)->get(url, params);
        if (handler != nullptr) {
            return handler;
        }
        params.clear();
    }
    return nullptr;
}
}
