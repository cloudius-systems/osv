/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ROUTES_HH_
#define ROUTES_HH_

#include "matchrules.hh"
#include "handlers.hh"
#include "request.hh"
#include "common.hh"
#include "reply.hh"

#include <boost/program_options/variables_map.hpp>
#include <unordered_map>
#include <vector>

namespace httpserver {

/**
 * The url helps defining a route.
 */
class url {
public:
    /**
     * Move constructor
     */
    url(url&&) = default;

    /**
     * Construct with a url path as it's parameter
     * @param path the url path to be used
     */
    url(const std::string& path) : path(path) {}

    /**
     * Adds a parameter that matches untill the end of the URL.
     * @param param the parmaeter name
     * @return the current url
     */
    url& remainder(const std::string& param)
    {
        this->param = param;
        return *this;
    }

    std::string path;
    std::string param;
};

/**
 * routes object do the request dispatching according to the url.
 * It uses two decision mechanism exact match, if a url matches exactly
 * (an optional leading slash is permitted) it is choosen
 * If not, the matching rules are used.
 * matching rules are evaluated by their insertion order
 */
class routes {
public:
    /**
     * The destructor deletes the match rules and handlers
     */
    ~routes();

    /**
     * adding a handler as an exact match
     * @param url the url to match (note that url should start with /)
     * @param handler the desire handler
     * @return it self
     */
    routes& put(operation_type type, const std::string& url,
                handler_base* handler)
    {
        map[type][url] = handler;
        return *this;
    }

    /**
     * add a rule to be used.
     * rules are search only if an exact match was not found.
     * rules are search by the order they were added.
     * First in higher priority
     * @param rule a rule to add
     * @param type the operation type
     * @return it self
     */
    routes& add(match_rule* rule, operation_type type = GET)
    {
        rules[type].push_back(rule);
        return *this;
    }

    /**
     * Add a url match to a handler:
     * Example  routes.add(GET, url("/api").remainder("path"), handler);
     * @param type
     * @param url
     * @param handler
     * @return
     */
    routes& add(operation_type type, const url& url, handler_base* handler);


    /**
     * uses the predefined path from swagger to configure the path
     * @param nick the nickname of the command as declared in
     * swagger api definition
     * @param handler a handler to be execute when the path is hit
     */
    routes& add_path(const std::string& nick, handler_base* handler);

    /**
     * the main entry point.
     * the general handler calls this method with the request
     * the method takes the headers from the request and find the
     * right handler.
     * It then call the handler with the parameters (if they exists) found in the url
     * @param path the url path found
     * @param req the http request
     * @param rep the http reply
     * @return true on success
     */
    bool handle(const std::string& path, const http::server::request& req,
                http::server::reply& rep);

private:

    /**
     * Search and return an exact match
     * @param url the request url
     * @return the handler if exists or nullptr if it does not
     */
    handler_base* get_exact_match(operation_type type,
                                  const std::string& url)
    {
        return (map[type].find(url) == map[type].end()) ?
               nullptr : map[type][url];
    }

    /**
     * Search and return a handler by the operation type and url
     * @param type the http operation type
     * @param url the request url
     * @param params a parameter object that will be filled during the match
     * @return a handler based on the type/url match
     */
    handler_base* get_handler(operation_type type, const std::string& url,
                              parameters& params);

    /**
     * Normalize the url to remove the last / if exists
     * and get the parameter part
     * @param url the full url path
     * @param param_part will hold the string with the parameters
     * @return the url from the request without the last /
     */
    std::string normalize_url(const std::string& url, std::string& param_part);

    std::unordered_map<std::string, handler_base*> map[2];
    std::vector<match_rule*> rules[2];
};

}

#endif /* ROUTES_HH_ */
