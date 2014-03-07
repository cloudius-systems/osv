/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "routes.hh"
#include "reply.hh"
#include "json/json_path.hh"

namespace httpserver {

using namespace std;

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

bool routes::handle(const string& path, const http::server::request& req,
                    http::server::reply& rep)
{
    parameters params;
    string param_str;

    handler_base* handler = get_handler(str2type(req.method),
                                        normalize_url(path, param_str), params);
    if (handler != nullptr) {
        try {
            handler->handle(path, &params, req, rep);
        } catch (const not_found_exception& e) {
            rep.content = e.what();
            rep.status = http::server::reply::status_type::not_found;
            handler->reply400(rep, 404, e.what());
        } catch (exception& e) {
            cerr << "exception was caught for " << path << ": " << e.what()
                 << endl;
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
                                  std::string& param_part)
{
    int param = url.find('?');
    if (param > 0) {
        param_part = url.substr(param, url.length() - param - 1);
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
