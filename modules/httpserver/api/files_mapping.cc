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
    file_handler* index = new file_handler("/usr/mgmt/swagger-ui/dist/index.html");
    routes.put(GET,"/",index);

    directory_handler* api = new directory_handler("/usr/mgmt/api",
            new content_replace("json"));
    routes.add(GET, url("/api").remainder("path"), api);
    routes.add(GET, url("").remainder("path"),
                   new directory_handler("/usr/mgmt/swagger-ui/dist"));
}

}
}
}
