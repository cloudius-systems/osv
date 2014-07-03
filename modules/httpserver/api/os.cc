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
#include <osv/debug.hh>
#include <osv/sched.hh>
#include <api/unistd.h>

extern char debug_buffer[DEBUG_BUFFER_SIZE];

namespace httpserver {

namespace api {

namespace os {

using namespace std;
using namespace json;
using namespace os_json;

void init(routes& routes)
{
    os_json_init_path();

    getOSversion.set_handler("json", [](const_req req)
    {
        return formatter::to_json(osv::version());
    });

    getOSmanufacturer.set_handler("json", [](const_req req)
    {
        return formatter::to_json("cloudius-systems");
    });

    getLastBootUpTime.set_handler("json",[](const_req req)
    {
        struct sysinfo info;
        sysinfo(&info);
        return formatter::to_json(info.uptime);
    });

    getDate.set_handler("json", [](const_req req)
    {
        time_t t;
        time(&t);
        date_time result;
        localtime_r(&t,&result);
        return formatter::to_json(result);
    });

    getTotalVirtualMemorySize.set_handler("json", [](const_req req)
    {
        struct sysinfo info;
        sysinfo(&info);
        return formatter::to_json(info.totalram);
    });

    getFreeVirtualMemory.set_handler("json", [](const_req req)
    {
        struct sysinfo info;
        sysinfo(&info);
        return formatter::to_json(info.freeram);
    });

    os_json::shutdown.set_handler("json", [](const_req req)
    {
        osv::shutdown();
        return formatter::to_json("");
    });

    getDebugMessages.set_handler("json", [](const_req req)
    {
        return string(debug_buffer);
    });

    getHostname.set_handler("json", [](const_req req)
    {
        char hostname[65];
        gethostname(hostname,65);
        return formatter::to_json(hostname);
    });

    setHostname.set_handler("json", [](const_req req)
    {
        string hostname = req.get_query_param("name");
        sethostname(hostname.c_str(), hostname.size());
        return formatter::to_json("");
    });

    listThreads.set_handler("json", [](const_req req)
    {
        using namespace std::chrono;
        httpserver::json::Threads threads;
        threads.time_ms = duration_cast<milliseconds>
            (osv::clock::wall::now().time_since_epoch()).count();
        httpserver::json::Thread thread;
        sched::with_all_threads([&](sched::thread &t) {
            thread.id = t.id();
            thread.cpu_ms = duration_cast<milliseconds>(t.thread_clock()).count();
            thread.name = t.name();
            threads.list.push(thread);
        });
        return formatter::to_json(threads);
    });

}

}
}
}
