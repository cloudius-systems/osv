/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PATH_HOLDER_HH_
#define PATH_HOLDER_HH_
#include "exception.hh"
#include "handlers.hh"
#include "routes.hh"

#pragma GCC visibility push(default)
namespace httpserver {
/**
 * Path holder glues handlers and implementation.
 * It allows simple assignment of a handler or a lambda expression to a route
 */
class path_holder {
public:
    explicit path_holder(const std::string& name)
        : name(name)
    {
    }

    /**
     * the path_holder object can be used as a constant of the
     * operation nickname
     */
    operator std::string() const
    {
        return name;
    }
    /**
     * Assign a handler to the path
     * @param h a handler to assign
     * @return a reference to the handler that was assign
     * throws an exception if h is null or if the routes was not set
     */
    handler_base& set_handler(handler_base* h) const;

    /**
     * assign a request function to the path
     * @param fun a request function
     * @param type the type of the reply, that would be used to set the headers
     * @return a reference to the handler that would be created
     */
    handler_base& set_handler(const std::string& type,
                              const request_function& fun) const;

    /**
     * assign a handle function to the path
     * @param fun a handle function
     * @param type the type of the reply, that would be used to set the headers
     * @return a reference to the handler that would be created
     */
    handler_base& set_handler(const std::string& type,
                              const handle_function& fun) const;
    /**
     * assign a request function to the path
     * set the type to json
     * @param fun a request function
     * @return a reference to the handler that would be created
     */
    handler_base& set_handler(const json_request_function& fun) const;

    /**
     * assign a handle function to the path
     * set the type to json
     * @param fun a handle function
     * @return a reference to the handler that would be created
     */
    handler_base& set_handler(const handle_function& fun) const {
        return set_handler("json", fun);
    }

    /**
     * set the routes object this should be done once when
     * the routes are created
     * @param route the route object
     */
    static void set_routes(routes* route)
    {
        _routes = route;
    }
private:
    std::string name;
    static routes* _routes;
};

}
#pragma GCC visibility pop
#endif /* PATH_HOLDER_HH_ */
