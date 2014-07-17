// request_handler.hpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//  * This file was modified from the original implementation *
//
//

#include "request_handler.hh"
#include "mime_types.hh"
#include "request.hh"
#include "reply.hh"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

namespace http {

namespace server {

size_t request_handler::update_parameters(request& req)
{
    auto update_param = [&req](const std::string & s, size_t bg, size_t end) {
        if (bg == end) {
            return;
        }
        req.query_parameters.push_back(header());
        size_t eq;
        if ((eq = s.find('=', bg)) < end) {
            req.query_parameters.back().name = s.substr(bg, eq - bg);
            request_handler::url_decode(s.substr(eq + 1, end - eq - 1),
                                        req.query_parameters.back().value);
        } else {
            req.query_parameters.back().name = s.substr(bg, end - bg);
            req.query_parameters.back().value = "";
        }
    };
    auto parse_str = [&](const std::string & s, size_t bg) {
        size_t end;
        while ((end = s.find('&', bg)) != std::string::npos) {
            update_param(s, bg, end);
            bg = end +1;
        }
        update_param(s, bg, s.length());
    };

    if (req.is_form_post()) {
        parse_str(req.content, 0);
    }

    size_t par = req.uri.find('?');
    if (par != std::string::npos) {
        parse_str(req.uri, par+1);
    }
    return par;
}

request_handler::request_handler(httpserver::routes* routes)
    : routes(routes)
{
}

void request_handler::handle_request(request& req, reply& rep)
{
    // Decode url to path.
    std::string request_path;
    size_t param = update_parameters(req);
    if (!url_decode(req.uri, request_path, param))
    {
        rep = reply::stock_reply(reply::bad_request);
        return;
    }

    // Request path must be absolute and not contain "..".
    if (request_path.empty() || request_path[0] != '/'
            || request_path.find("..") != std::string::npos)
    {

        rep = reply::stock_reply(reply::bad_request);
        return;
    }

    routes->handle(request_path, req, rep);
}

bool request_handler::url_decode(const std::string& in, std::string& out,
                                 size_t max)
{
    out.clear();
    out.reserve(in.size());
    if (in.size() < max) {
        max = in.size();
    }

    for (std::size_t i = 0; i < max; ++i)
    {
        if (in[i] == '%')
        {
            if (i + 3 <= in.size())
            {
                int value = 0;
                std::istringstream is(in.substr(i + 1, 2));
                if (is >> std::hex >> value)
                {
                    out += static_cast<char>(value);
                    i += 2;
                }
                else
                {
                    return false;
                }
            }
            else
            {
                return false;
            }
        }
        else if (in[i] == '+')
        {
            out += ' ';
        }
        else
        {
            out += in[i];
        }
    }
    return true;
}

} // namespace server

} // namespace http
