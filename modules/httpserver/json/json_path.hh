/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef JSON_PATH_HH_
#define JSON_PATH_HH_

#include <string>
#include <vector>
#include <unordered_map>
#include <tuple>
#include "common.hh"

namespace httpserver {

namespace json {

/**
 * A json_operation contain a method and a nickname.
 * operation are associated to a path, that can
 * have multiple methods
 */
struct json_operation {
    /**
     * default constructor
     */
    json_operation()
        : method(GET)
    {
    }

    /**
     * Construct with assignment
     * @param method the http method type
     * @param nickname the http nickname
     */
    json_operation(operation_type method, const std::string& nickname)
        :
        method(method), nickname(nickname)
    {
    }

    operation_type method;
    std::string nickname;

};

/**
 * path description holds the path in the system.
 * It maps a nickname to an operation, which allows
 * defining the operation (path and method) by its
 * nickname.
 *
 * the description are taken from the json swagger
 * definition file, during auto code generation in the
 * compilation.
 */
struct path_description {

    /**
     * add a path to the path list.
     * @param path the path url
     * @param method the http method
     * @param nickname the nickname
     * @return the newly created path description
     */
    static path_description* add_path(std::string path, operation_type method,
                                      std::string nickname);

    /**
     * search for a path description (path and command) with a given nickname
     * @param nickname the nickname of the path description
     * @return the path description if it is found or nullptrf
     */
    static path_description* get(std::string nickname);



    /**
     * default empty constructor
     */
    path_description() = default;

    /**
     * constructor for path with parameters
     * @param path the url path
     * @param method the http method
     * @param nickname the nickname
     */
    path_description(const std::string& path, operation_type method,
                     std::string nickname)
        : path(path), operations(method, nickname)
    {

    }

    /**
     * Add a parameter to the path definition
     * for example, if the url should match /file/{path}
     * The constructor would be followed by a call to
     * pushparam("path")
     *
     * @param param the name of the parameters, this name will
     * be used by the handler to identify the parameters.
     * A name can appear at most once in a description
     * @param all_path when set to true the parameter will assume to match
     * until the end of the url.
     * This is useful for situation like file path with
     * a rule like /file/{path} and a url /file/etc/hosts.
     * path should be equal to /ets/hosts and not only /etc
     * @return the current path description
     */
    path_description* pushparam(const std::string& param,
                                bool all_path = false)
    {
        params.push_back(std::make_tuple(param, all_path));
        return this;
    }

    std::vector<std::tuple<std::string, bool>> params;
    std::string path;
    json_operation operations;

    static std::unordered_map<std::string, path_description*> path_by_nickname;
};

}
}
#endif /* JSON_PATH_HH_ */
