/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/utsname.h>
#include <sys/time.h>
#include "os.hh"
#include "json/formatter.hh"
#include "autogen/os.json.hh"
#include <sys/sysinfo.h>
#include <time.h>
#include <osv/shutdown.hh>
#include <osv/power.hh>
#include <api/unistd.h>
#include <osv/commands.hh>
#include <osv/osv_c_wrappers.h>
#include <algorithm>
#include "../java-base/balloon/balloon_api.hh"

namespace httpserver {

namespace api {

namespace os {

using namespace std;
using namespace json;
using namespace os_json;

#if !defined(MONITORING)
extern "C" void httpserver_plugin_register_routes(httpserver::routes* routes) {
    httpserver::api::os::init(*routes);
}
#endif

static std::string from_c_string(char *c_str) {
    if (c_str) {
        std::string str(c_str);
        free(c_str);
        return str;
    } else {
        return std::string();
    }
}

void init(routes& routes)
{
    os_json_init_path("OS core API");

    os_name.set_handler([](const_req req){
        return "OSv";
    });

    os_version.set_handler([](const_req req) {
        return from_c_string(osv_version());
    });

    os_vendor.set_handler([](const_req req) {
        return "Cloudius Systems";
    });

    os_uptime.set_handler([](const_req req) {
        struct sysinfo info;
        sysinfo(&info);
        return info.uptime;
    });

    os_date.set_handler([](const_req req) {
        time_t t;
        time(&t);
        date_time result;
        localtime_r(&t,&result);
        return result;
    });

    os_memory_total.set_handler([](const_req req) {
        struct sysinfo info;
        sysinfo(&info);
        return info.totalram;
    });

    os_memory_free.set_handler([](const_req req) {
        struct sysinfo info;
        sysinfo(&info);
        return info.freeram;
    });

    os_memory_balloon.set_handler([](const_req req) {
        return memory::get_balloon_size();
    });

#if !defined(MONITORING)
    os_shutdown.set_handler([](const_req req) {
        osv::shutdown();
        return "";
    });

    os_poweroff.set_handler([](const_req req) {
            osv::poweroff();
            return "";
        });

    os_reboot.set_handler([](const_req req) {
        osv::reboot();
        return "";
    });
#endif

    os_dmesg.set_handler([](const_req req) {
        return osv_debug_buffer();
    });

    os_get_hostname.set_handler([](const_req req)
    {
        char hostname[65];
        gethostname(hostname,65);
        return json_return_type(hostname);
    });

#if !defined(MONITORING)
    os_set_hostname.set_handler([](const_req req) {
        string hostname = req.get_query_param("name");
        sethostname(hostname.c_str(), hostname.size());
        return "";
    });
#endif

    os_threads.set_handler([](const_req req) {
        httpserver::json::Threads threads;
        timeval timeofday;
        if (gettimeofday(&timeofday, nullptr)) {
            return threads;
        }
        threads.time_ms = timeofday.tv_sec * 1000 + timeofday.tv_usec / 1000;
        httpserver::json::Thread thread;
        osv_thread *osv_threads;
        size_t threads_num;
        if (!osv_get_all_threads(&osv_threads, &threads_num)) {
            for (size_t i = 0; i < threads_num; i++) {
                auto &t = osv_threads[i];
                thread.id = t.id;
                thread.status = t.status;
                thread.cpu = t.cpu_id;
                thread.cpu_ms = t.cpu_ms;
                thread.switches = t.switches;
                thread.migrations = t.migrations;
                thread.preemptions = t.preemptions;
                thread.name = t.name;
                free(t.name);
                thread.priority = t.priority;
                thread.stack_size = t.stack_size;
                thread.status = t.status;
                threads.list.push(thread);
            }
            free(osv_threads);
        }
        return threads;
    });

    os_get_cmdline.set_handler([](const_req req) {
        return from_c_string(osv_cmdline());
    });

#if !defined(MONITORING)
    os_set_cmdline.set_handler([](const_req req) {
        string newcmd = req.get_query_param("cmdline");

        try {
            osv::save_cmdline(newcmd);
        } catch(std::length_error &e) {
            throw bad_request_exception("command line too long");
        } catch(std::system_error &e) {
            throw server_error_exception(e.what());
        }

        return osv::getcmdline();

    });
#endif

}

}
}
}
