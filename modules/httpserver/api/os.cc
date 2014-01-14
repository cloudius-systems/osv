/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/utsname.h>
#include "os.hh"
#include "osv/version.hh"
#include "json/formatter.hh"

namespace httpserver {

namespace api {

namespace os {
using namespace std;

void init(routes& routes)
{
    function_handler* os_version = new function_handler([](const_req req)
    {
        return json::formatter::to_json(osv::version());
    }, "json");

    routes.put(GET, "/os/version", os_version);
}

}
}
}
