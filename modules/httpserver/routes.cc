/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "routes.hh"
#include "reply.hh"
#include "json/json_path.hh"
#include "exception.hh"

namespace httpserver {

using namespace std;

void verify_param(const http::server::request& req, const std::string& param) {
    if (req.get_query_param(param) == "") {
        throw missing_param_exception(param);
    }
}

routes::~routes()
{
    for (int i = 0; i < NUM_OPERATION; i++) {
        for (auto kv : map[i]) {
            delete kv.second;
        }
    }
    for (int i = 0; i < NUM_OPERATION; i++) {
        for (auto r : rules[i]) {
            delete r;
        }
    }

}

void routes::handle(const string& path, http::server::request& req,
                    http::server::reply& rep)
{
    handler_base* handler = get_handler(str2type(req.method),
                                        normalize_url(path), req.param);
    if (handler != nullptr) {
        try {
            for (auto& i : handler->mandatory_param) {
                verify_param(req, i);
            }
            handler->handle(path, &req.param, req, rep);
        } catch (const base_exception& _e) {
            json_exception e(_e);
            rep.content = e.to_json();
            rep.status = _e.status();
            handler->set_headers(rep, "json");
        } catch (exception& _e) {
            json_exception e(_e);
            cerr << "exception was caught for " << path << ": " << _e.what()
                 << endl;
            rep.content = e.to_json();
            rep.status = http::server::reply::internal_server_error;
            handler->set_headers(rep, "json");
            return;
        }
    } else {
        rep = http::server::reply::stock_reply(http::server::reply::not_found);
    }
}

std::string routes::normalize_url(const std::string& url)
{
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

    for (auto rule = rules[type].cbegin(); rule != rules[type].cend(); ++rule) {
        handler = (*rule)->get(url, params);
        if (handler != nullptr) {
            return handler;
        }
        params.clear();
    }
    return nullptr;
}

routes& routes::add_path(const string& nick, handler_base* handler)
{
    json::path_description* path = json::path_description::get(nick);
    if (path == nullptr) {
        cerr << "Failed adding path by nickname, no path found for nickname "
             << nick << endl;
        return *this;
    }
    for (auto& i : path->mandatory_queryparams) {
        handler->mandatory(i);
    }

    if (path->params.size() == 0)
        put(path->operations.method, path->path, handler);
    else {
        match_rule* rule = new match_rule(handler);
        rule->add_str(path->path);
        for (auto i = path->params.begin(); i != path->params.end(); ++i) {
            rule->add_param(std::get<0>(*i), std::get<1>(*i));
        }
        add(rule, path->operations.method);
    }
    return *this;
}

routes& routes::add(operation_type type, const url& url, handler_base* handler)
{
    match_rule* rule = new match_rule(handler);
    rule->add_str(url.path);
    rule->add_param(url.param, true);
    return add(rule, type);
}

}
