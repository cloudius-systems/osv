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

    request_function os_version = [](const_req req)
    {
        return formatter::to_json(osv::version());
    };
    getOSversion.set_handler(os_version, "json");

    request_function manufacturer = [](const_req req)
    {
        return formatter::to_json("cloudius-systems");
    };
    getOSmanufacturer.set_handler(manufacturer, "json");

    request_function bootup = [](const_req req)
    {
        struct sysinfo info;
        sysinfo(&info);
        return formatter::to_json(info.uptime);
    };

    getLastBootUpTime.set_handler(bootup, "json");

    request_function get_date = [](const_req req)
    {
        time_t t;
        time(&t);
        date_time result;
        localtime_r(&t,&result);
        return formatter::to_json(result);
    };
    getDate.set_handler(get_date, "json");

    request_function total_mem = [](const_req req)
    {
        struct sysinfo info;
        sysinfo(&info);
        return formatter::to_json(info.totalram);
    };
    getTotalVirtualMemorySize.set_handler(total_mem, "json");

    request_function free_mem = [](const_req req)
    {
        struct sysinfo info;
        sysinfo(&info);
        return formatter::to_json(info.freeram);
    };
    getFreeVirtualMemory.set_handler(free_mem, "json");

    request_function shutdown = [](const_req req)
    {
        osv::shutdown();
        return formatter::to_json("");
    };
    os_json::shutdown.set_handler(shutdown, "json");

    request_function dmesg = [](const_req req)
    {
        return string(debug_buffer);
    };
    getDebugMessages.set_handler(dmesg, "json");
}

}
}
}
