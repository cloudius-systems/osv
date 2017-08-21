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
    GET, POST, PUT, DELETE, OPTIONS, NUM_OPERATION
};

/**
 * Translate the string command to operation type
 * @param type the string "GET" or "POST"
 * @return the operation_type
 */
operation_type str2type(const std::string& type);

namespace api {

/**
 * Convert string to bool value. If input is invalid, exception is raised.
 * @param val  string "true" or "1" -> true, "false", "0" or "" -> false.
 * @return the boolean value
 */
bool str2bool(std::string val);

}

}

#endif /* COMMON_HH_ */
