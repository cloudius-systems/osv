/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef COMMON_HH_
#define COMMON_HH_

#include <unordered_map>
#include <string>

namespace httpserver {

typedef std::unordered_map<std::string, std::string> parameters;

enum operation_type {
    GET, POST
};

/**
 * Translate the string command to operation type
 * @param type the string "GET" or "POST"
 * @return the operation_type
 */
operation_type str2type(const std::string& type);

}

#endif /* COMMON_HH_ */
