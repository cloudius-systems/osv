/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include "common.hh"

#include "request.hh"
#include "exception.hh"

namespace httpserver {

operation_type str2type(const std::string& type)
{
    if (type == "DELETE") {
        return DELETE;
    }
    if (type == "POST") {
        return POST;
    }
    if (type == "PUT") {
        return PUT;
    }
    if (type == "OPTIONS") {
        return OPTIONS;
    }
    return GET;
}

namespace api {

bool str2bool(std::string val)
{
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    if (val == "" || val == "false" || val == "0") {
        return false;
    }
    if (val == "true" || val == "1") {
        return true;
    }
    throw bad_param_exception(std::string("Invalid value ") + val + " use true/false or 0/1");
    return true;
}

}

}
