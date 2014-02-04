//
// request.hpp
// ~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef HTTP_REQUEST_HPP
#define HTTP_REQUEST_HPP

#include "header.hh"

#include <string>
#include <vector>

namespace http {

namespace server {

/**
 * A request received from a client.
 */
struct request {
    std::string method;
    std::string uri;
    int http_version_major;
    int http_version_minor;
    std::vector<header> headers;

    /**
     * Search for the first header of a given name
     * @param name the header name
     * @return a pointer to the header value, if it exists or empty string
     */
    std::string get_header(const std::string& name) const {
        for (auto h: headers) {
            if (h.name == name) {
                return h.value;
            }
        }
        return "";
    }
};

} // namespace server

} // namespace http

#endif // HTTP_REQUEST_HPP
