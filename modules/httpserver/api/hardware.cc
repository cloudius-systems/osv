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
#include <osv/sched.hh>
#include <osv/firmware.hh>
#include <osv/hypervisor.hh>

namespace httpserver {

namespace api {

namespace hardware {

using namespace std;
using namespace json;
using namespace hardware_json;

void init(routes& routes)
{
    hardware_json_init_path("Hardware management API");

    processorFeatures.set_handler([](const_req req)
    {
        return processor::features_str();
    });

    processorCount.set_handler([](const_req req)
    {
        return sched::cpus.size();
    });

    firmware_vendor.set_handler([](const_req) {
        return osv::firmware_vendor();
    });

    get_hypervisor.set_handler([](const_req) {
        switch (osv::hypervisor()) {
        case osv::hypervisor_type::kvm: return "kvm";
        case osv::hypervisor_type::xen: return "xen";
        case osv::hypervisor_type::vmware_workstation: return "vmware_workstation";
        case osv::hypervisor_type::vmware_esxi: return "vmware_esxi";
        default:
            return "Unknown";
        }

    });
}

}
}
}
