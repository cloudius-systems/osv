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
#include <strings.h>
#include "common.hh"

namespace http {

namespace server {

class connection;

/**
 * A request received from a client.
 */
struct request {
    enum class ctclass : char {
        other,
        multipart,
        app_x_www_urlencoded,
    };

    std::string method;
    std::string uri;
    int http_version_major;
    int http_version_minor;
    ctclass content_type_class;
    size_t content_length = 0;
    std::vector<header> headers;
    std::vector<header> query_parameters;
    connection* connection_ptr;
    httpserver::parameters param;
    std::string content;
    std::string protocol_name;

    /**
     * Search for the first header of a given name
     * @param name the header name
     * @return a pointer to the header value, if it exists or empty string
     */
    std::string get_header(const std::string& name) const
    {
        return find_in_vector(headers, name, false); // http headers are case insensitive
    }

    /**
     * Search for the first header of a given name
     * @param name the header name
     * @return a pointer to the header value, if it exists or empty string
     */
    std::string get_query_param(const std::string& name) const
    {
        return find_in_vector(query_parameters, name);
    }

    /**
     * Get the request protocol name. Can be either "http" or "https".
     */
    std::string get_protocol_name() const
    {
        return protocol_name;
    }

    /**
     * Get the request url.
     * @return the request url
     */
    std::string get_url() const
    {
        return get_protocol_name() + "://" + get_header("Host") + uri;
    }

    bool is_multi_part() const {
        return content_type_class == ctclass::multipart;
    }
    bool is_form_post() const {
        return content_type_class == ctclass::app_x_www_urlencoded;
    }

    static std::string find_in_vector(const std::vector<header>& vec,
                                      const std::string& name,
                                      bool case_sensitive = true)
    {
        for (auto h : vec) {
            auto eq =
                    case_sensitive ?
                            h.name == name :
                            h.name.size() == name.size()
                                    && !::strcasecmp(h.name.c_str(),
                                            name.c_str())
                      ;
            if (eq) {
                return h.value;
            }
        }
        return "";
    }
};

} // namespace server

} // namespace http

#endif // HTTP_REQUEST_HPP
