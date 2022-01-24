/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "fs.hh"
#include "json/formatter.hh"
#include "autogen/fs.json.hh"
#include <string>
#include <vector>
#include <sys/statvfs.h>
#include <mntent.h>

namespace httpserver {

namespace api {

namespace fs {

using namespace std;
using namespace json;
using namespace fs_json;

static void fill_dfstat(DFStat& dfstat, mntent* mount, const struct statvfs& st) {
    dfstat.filesystem = mount->mnt_fsname;
    dfstat.mount = mount->mnt_dir;
    dfstat.btotal = st.f_blocks;
    dfstat.bfree = st.f_bfree;
    dfstat.ftotal = st.f_files;
    dfstat.ffree = st.f_ffree;
    dfstat.blocksize = st.f_frsize;
}

#if !defined(MONITORING)
extern "C" void httpserver_plugin_register_routes(httpserver::routes* routes) {
    httpserver::api::fs::init(*routes);
}
#endif

void init(routes& routes) {

    fs_json_init_path("FS core API");

    getDFStats.set_handler("json",
                           [](const_req req)
    {
        const std::string onemount = req.param.at("mount");
        struct statvfs st;
        httpserver::json::DFStat dfstat;
        vector<httpserver::json::DFStat> dfstats;

        FILE *mounts_fp = setmntent("/proc/mounts", "r");
        if (!mounts_fp) {
            throw server_error_exception("failed to get mounts information");
        }

        struct mntent* mount;
        mntent mnt;
        char strings[4096];
        while ((mount = getmntent_r(mounts_fp, &mnt, strings, sizeof(strings)))) {
            std::string fstype(mount->mnt_type);
            if ((fstype == "zfs" || fstype == "rofs") && (onemount == "" || onemount == mount->mnt_dir)) {
                if (statvfs(mount->mnt_dir,&st) != 0) {
                    endmntent(mounts_fp);
                    throw not_found_exception("mount does not exist");
                }
                fill_dfstat(dfstat, mount, st);
                dfstats.push_back(dfstat);
            }
        };
        endmntent(mounts_fp);

        // checking if a specific file system was requested and if we found it
        if (onemount != "" && dfstats.size() == 0) {
            throw not_found_exception("mount does not exist");
        }

        return formatter::to_json(dfstats);
    });

    list_df_stats.set_handler([](const_req req)
        {
            struct statvfs st;
            httpserver::json::DFStat dfstat;
            vector<httpserver::json::DFStat> res;

            FILE *mounts_fp = setmntent("/proc/mounts", "r");
            if (!mounts_fp) {
                throw server_error_exception("failed to get mounts information");
            }

            struct mntent* mount;
            mntent mnt;
            char strings[4096];
            while ((mount = getmntent_r(mounts_fp, &mnt, strings, sizeof(strings)))) {
                std::string fstype(mount->mnt_type);
                if (fstype == "zfs" || fstype == "rofs") {
                    if (statvfs(mount->mnt_dir,&st) == 0) {
                        fill_dfstat(dfstat, mount, st);
                        res.push_back(dfstat);
                    }
                }
            }
            endmntent(mounts_fp);
            return res;
        });

}

}
}
}
