/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "app.hh"
#include "autogen/app.json.hh"
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <osv/commands.hh>
#include <osv/app.hh>

namespace httpserver {

namespace api {

namespace app {

using namespace std;
using namespace json;
using namespace app_json;

static std::string exec_app(const std::string& cmnd_line) {
    bool ok;
    auto new_commands = osv::parse_command_line(cmnd_line, ok);
    if (!ok) {
        throw bad_param_exception("Bad formatted command");
    }
    for (auto cmnd: new_commands) {
        auto suffix = cmnd.back();
        if (suffix == ";") {
            throw bad_param_exception("The use of ; is not allowed, use & for multiple commands");
        }
    }
    std::string app_ids;
    for (auto cmnd: new_commands) {
        std::vector<std::string> c(cmnd.begin(), std::prev(cmnd.end()));
        auto app = osv::application::run(c);
        pid_t pid = app->get_main_thread_id();
        assert(pid != 0);
        app_ids += std::to_string(pid) + " ";
    }
    if (app_ids.size()) {
        app_ids.pop_back(); // remove trailing space
    }
    return app_ids;
}

void init(routes& routes)
{
    app_json_init_path("app API");
    run_app.set_handler([](const_req req) {
        string command = req.get_query_param("command");
        return exec_app(command);
    });
}
}
}
}
