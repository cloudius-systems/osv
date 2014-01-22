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

namespace httpserver {

namespace api {

namespace os {

using namespace std;
using namespace json;

void init(routes& routes)
{
    os_json_init_path();

    function_handler* os_version = new function_handler([](const_req req)
    {
        return formatter::to_json(osv::version());
    }, "json");
    routes.add_path("getOSversion", os_version);

    function_handler* manufacturer = new function_handler([](const_req req)
    {
        return formatter::to_json("cloudius-systems");
    }, "json");
    routes.add_path("getOSmanufacturer", manufacturer);

    function_handler* bootup = new function_handler([](const_req req)
    {
        struct sysinfo info;
        sysinfo(&info);
        return formatter::to_json(info.uptime);
    }, "json");
    routes.add_path("getLastBootUpTime", bootup);

    function_handler* get_date = new function_handler([](const_req req)
    {
        time_t t;
        time(&t);
        date_time result;
        localtime_r(&t,&result);
        return formatter::to_json(result);
    }, "json");
    routes.add_path("getDate", get_date);

    function_handler* total_mem = new function_handler([](const_req req)
    {
        struct sysinfo info;
        sysinfo(&info);
        return formatter::to_json(info.totalram);
    }, "json");
    routes.add_path("getTotalVirtualMemorySize", total_mem);

    function_handler* free_mem = new function_handler([](const_req req)
    {
        struct sysinfo info;
        sysinfo(&info);
        return formatter::to_json(info.freeram);
    }, "json");
    routes.add_path("getFreeVirtualMemory", free_mem);

    function_handler* shutdown = new function_handler([](const_req req)
    {
        osv::shutdown();
        return formatter::to_json("");
    }, "json");
    routes.add_path("shutdown", shutdown);
}

}
}
}
