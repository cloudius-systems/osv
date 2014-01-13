/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * This file will hold specific handler implementations.
 * General handlers comes with the handlers definitions
 */

#ifndef HTTPSERVER_API_OS_HH_
#define HTTPSERVER_API_OS_HH_

#include "routes.hh"

namespace httpserver {

namespace api {

namespace os {

/**
 * Initialize the routes object with specific routes mapping
 * @param routes - the routes object to fill
 */
void init(routes& routes);

}
}
}
#endif /* HTTPSERVER_API_OS_HH_ */
