/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "hardware.hh"
#include "json/formatter.hh"
#include "autogen/hardware.json.hh"
#include "processor.hh"
#include "cpuid.hh"
#include <osv/osv_c_wrappers.h>
#include <sys/sysinfo.h>

namespace httpserver {

namespace api {

namespace hardware {

using namespace std;
using namespace json;
using namespace hardware_json;

#if !defined(MONITORING)
extern "C" void httpserver_plugin_register_routes(httpserver::routes* routes) {
    httpserver::api::hardware::init(*routes);
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
    hardware_json_init_path("Hardware management API");

    processorFeatures.set_handler([](const_req req)
    {
        return from_c_string(osv_processor_features());
    });

    processorCount.set_handler([](const_req req)
    {
        return get_nprocs();
    });

    firmware_vendor.set_handler([](const_req) {
        return from_c_string(osv_firmware_vendor());
    });

    hypervisor_name.set_handler([](const_req) {
        return from_c_string(osv_hypervisor_name());
    });
}

}
}
}
