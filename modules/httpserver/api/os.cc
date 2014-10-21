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
#include "autogen/os.json.hh"
#include <sys/sysinfo.h>
#include <time.h>
#include <osv/shutdown.hh>
#include <osv/power.hh>
#include <osv/debug.hh>
#include <osv/sched.hh>
#include <api/unistd.h>
#include <osv/commands.hh>
#include <algorithm>
#include "java/balloon_api.hh"

extern char debug_buffer[DEBUG_BUFFER_SIZE];

namespace httpserver {

namespace api {

namespace os {

using namespace std;
using namespace json;
using namespace os_json;

void init(routes& routes)
{
    os_json_init_path("OS core API");

    os_name.set_handler([](const_req req){
        return "OSv";
    });

    os_version.set_handler([](const_req req) {
        return osv::version();
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

    os_shutdown.set_handler([](const_req req) {
        osv::shutdown();
        return "";
    });

    os_reboot.set_handler([](const_req req) {
        osv::reboot();
        return "";
    });

    os_dmesg.set_handler([](const_req req) {
        return debug_buffer;
    });

    os_get_hostname.set_handler([](const_req req)
    {
        char hostname[65];
        gethostname(hostname,65);
        return json_return_type(hostname);
    });

    os_set_hostname.set_handler([](const_req req) {
        string hostname = req.get_query_param("name");
        sethostname(hostname.c_str(), hostname.size());
        return "";
    });

    os_threads.set_handler([](const_req req) {
        using namespace std::chrono;
        httpserver::json::Threads threads;
        threads.time_ms = duration_cast<milliseconds>
            (osv::clock::wall::now().time_since_epoch()).count();
        httpserver::json::Thread thread;
        sched::with_all_threads([&](sched::thread &t) {
            thread.id = t.id();
            thread.status = t.get_status();
            auto tcpu = t.tcpu();
            thread.cpu = tcpu ? tcpu->id : -1;
            thread.cpu_ms = duration_cast<milliseconds>(t.thread_clock()).count();
            thread.switches = t.stat_switches.get();
            thread.migrations = t.stat_migrations.get();
            thread.preemptions = t.stat_preemptions.get();
            thread.name = t.name();
            threads.list.push(thread);
        });
        return threads;
    });

    os_get_cmdline.set_handler([](const_req req) {
        return osv::getcmdline();
    });

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

}

}
}
}
