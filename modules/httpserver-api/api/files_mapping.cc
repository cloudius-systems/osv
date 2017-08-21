/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "files_mapping.hh"
#include "routes.hh"
#include "transformers.hh"
namespace httpserver {

namespace api {

namespace files_mapping {

void init(routes& routes)
{
    function_handler* redirect =
        new function_handler(
    [](const_req req) {
        throw redirect_exception(req.get_protocol_name() + "://" +
                req.get_header("Host") + "/dashboard/");
        // The return is required so the lambda expression would have
        // the right signature
        return "";
    });
    routes.put(GET, "/", redirect);
    file_handler* index = new file_handler(
        "/usr/mgmt/swagger-ui/dist/index.html", nullptr, true);
    routes.put(GET, "/api-gui", index);

    directory_handler* api = new directory_handler("/usr/mgmt/api",
            new content_replace("json"));
    routes.add(GET, url("/api").remainder("path"), api);
    routes.add(GET, url("/dashboard_static").remainder("path"),
               new directory_handler("/usr/mgmt/gui/dashboard_static"));
    routes.add(GET, url("/dashboard").remainder("path"),
               new file_handler("/usr/mgmt/gui/dashboard/index.html"));
    routes.add(GET, url("/api-gui").remainder("path"),
               new directory_handler("/usr/mgmt/swagger-ui/dist"));
}

}
}
}
