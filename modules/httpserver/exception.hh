/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef EXCEPTION_HH_
#define EXCEPTION_HH_

#include "reply.hh"

namespace httpserver {

/**
 * The base_exception is a base for all httpserver exception.
 * It contains a message that will be return as the message content
 * and a status that will be return as a status code.
 */
class base_exception : public std::exception {
public:
    base_exception(const std::string& msg,
                   http::server::reply::status_type status)
        : _msg(msg), _status(status)
    {
    }

    virtual const char* what() const throw ()
    {
        return _msg.c_str();
    }

    http::server::reply::status_type status() const
    {
        return _status;
    }
private:
    std::string _msg;
    http::server::reply::status_type _status;

};

/**
 * Throwing this exception will result in a 404 not found result
 */
class not_found_exception : public base_exception
{
public:
    not_found_exception(const std::string& msg)
        : base_exception(msg, http::server::reply::not_found)
    {
    }
};

/**
 * Throwing this exception will result in a 400 bad request result
 */

class bad_request_exception : public base_exception
{
public:
    bad_request_exception(const std::string& msg)
        : base_exception(msg, http::server::reply::bad_request)
    {
    }
};

class bad_param_exception : public bad_request_exception
{
public:
    bad_param_exception(const std::string& msg)
        : bad_request_exception(msg)
    {
    }
};


class missing_param_exception : public bad_request_exception
{
public:
    missing_param_exception(const std::string& param)
        : bad_request_exception(std::string("Missing mandatory parameter '") + param +"'")
    {
    }
};

class server_error_exception : public base_exception
{
public:
    server_error_exception(const std::string& msg)
        : base_exception(msg, http::server::reply::internal_server_error)
    {
    }
};

}

#endif /* EXCEPTION_HH_ */
