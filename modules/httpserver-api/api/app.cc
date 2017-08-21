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
#include <osv/sched.hh>

namespace httpserver {

namespace api {

namespace app {

using namespace std;
using namespace json;
using namespace app_json;

static std::string exec_app(const std::string& cmnd_line, bool new_program) {
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
        auto app = osv::application::run(c[0], c, new_program);
        pid_t pid = app->get_main_thread_id();
        assert(pid != 0);
        app_ids += std::to_string(pid) + " ";
    }
    if (app_ids.size()) {
        app_ids.pop_back(); // remove trailing space
    }
    return app_ids;
}

extern "C" void httpserver_plugin_register_routes(httpserver::routes* routes) {
    httpserver::api::app::init(*routes);
}

void init(routes& routes)
{
    app_json_init_path("app API");

    run_app.set_handler([](const_req req) {
        string command = req.get_query_param("command");
        bool new_program = str2bool(req.get_query_param("new_program"));
        return exec_app(command, new_program);
    });

    finished_app.set_handler([](const_req req) {
        std::string tid_str = req.get_query_param("tid");
        // TODO accept as input a space separated list too ?
        pid_t tid = std::stol(tid_str);
        // If the app did finish and was joined (or started detached), thread no longer exists.
        // Or such thread never existed. In both cases, report thread as finished.
        const char* th_finished = "1";
        sched::with_thread_by_id(tid, [&](sched::thread *t) {
            if (t && t->get_status() != sched::thread::status::terminated) {
                th_finished = "0";
            }
        });
        return th_finished;
    });

}
}
}
}
