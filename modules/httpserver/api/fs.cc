/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "fs.hh"
#include "osv/mount.h"
#include "json/formatter.hh"
#include "autogen/fs.json.hh"
#include <string>
#include <vector>
#include <sys/vfs.h>

namespace httpserver {

namespace api {

namespace fs {

using namespace std;
using namespace json;
using namespace fs_json;

void init(routes& routes) {

    fs_json_init_path();

    getDFStats.set_handler("json",
                           [](const_req req)
    {
        using namespace osv;
        const std::string onemount = req.param.at("mount");
        struct statfs st;
        httpserver::json::DFStat dfstat;
        vector<httpserver::json::DFStat> dfstats;

        for (mount_desc mount : osv::current_mounts()) {
            if (mount.type == "zfs" && (onemount == "" || onemount == mount.path)) {
                if (statfs(mount.path.c_str(),&st) != 0) {
                    throw not_found_exception("mount does not exist");
                }
                dfstat.filesystem = mount.special;
                dfstat.mount = mount.path;
                dfstat.btotal = st.f_blocks;
                dfstat.bfree = st.f_bfree;
                dfstat.ftotal = st.f_files;
                dfstat.ffree = st.f_ffree;

                dfstats.push_back(dfstat);
            }
        };

        // checking if a specific file system was requested and if we found it
        if (onemount != "" && dfstats.size() == 0) {
            throw not_found_exception("mount does not exist");
        }

        return formatter::to_json(dfstats);
    });

}

}
}
}
