//
// request_handler.hpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//
//  * This file was modified from the original implementation *

#ifndef HTTP_REQUEST_HANDLER_HPP
#define HTTP_REQUEST_HANDLER_HPP

#include "routes.hh"

#include <boost/program_options/variables_map.hpp>
#include <string>

namespace http {

namespace server {

struct reply;
struct request;

/**
 * The common handler for all incoming requests.
 * the handler was changed from its original implementation by passing it
 * a config object and routes object.
 * the config object holds any configuration if needed.
 * the routes object handle routing of request to a handler
 */
class request_handler {
public:

    /**
     * Construct with the configuration and routes
     *
     * @param config the configuration object
     * @param routes the routes object
     */
    explicit request_handler(const boost::program_options::variables_map* config,
                             httpserver::routes* routes);

    request_handler(const request_handler&) = delete;

    request_handler& operator=(const request_handler&) = delete;

    /**
     * Handle a request and produce a reply.
     *
     * @param req the request to handle
     * @param rep the reply
     */
    void handle_request(const request& req, reply& rep);

private:
    const boost::program_options::variables_map* config;
    httpserver::routes* routes;

    /**
     * Perform URL-decoding on a string. Returns false if the encoding was
     * invalid.
     * @param in the input url string
     * @param out the decode url string
     * @return true on success
     */
    static bool url_decode(const std::string& in, std::string& out);
};

} // namespace server

} // namespace http

#endif // HTTP_REQUEST_HANDLER_HPP
