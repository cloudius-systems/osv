/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include "path_holder.hh"
#include <assert.h>

namespace httpserver {

routes* path_holder::_routes = nullptr;

handler_base& path_holder::set_handler(handler_base* h) const
{
    assert(_routes != nullptr && h != nullptr);
    _routes->add_path(name, h);
    return *h;
}

handler_base& path_holder::set_handler(const std::string& type,
                                       const request_function& fun) const
{
    return set_handler(new function_handler(fun, type));
}

handler_base& path_holder::set_handler(const std::string& type,
                                       const handle_function& fun) const
{
    return set_handler(new function_handler(fun, type));
}

}
