/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/hypervisor.hh>

#include <osv/firmware.hh>

#include "cpuid.hh"

namespace osv {

hypervisor_type hypervisor()
{
    if (processor::features().kvm_clocksource || processor::features().kvm_clocksource2) {
        return hypervisor_type::kvm;
    }
    if (processor::features().xen_clocksource || firmware_vendor() == "Xen") {
        return hypervisor_type::xen;
    }
    return hypervisor_type::unknown;
}

std::string hypervisor_name()
{
    switch (osv::hypervisor()) {
    case osv::hypervisor_type::kvm:
        return "kvm";
    case osv::hypervisor_type::xen:
        return "xen";
    default:
        return "Unknown";
    }
}

}
