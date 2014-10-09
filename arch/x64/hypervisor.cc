/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/hypervisor.hh>

#include "cpuid.hh"

namespace osv {

static constexpr uint32_t vmware_magic  = 0x564D5868;
static constexpr uint16_t vmware_port   = 0x5658;
enum vmware_cmd {
    vmware_cmd_getversion       = 10,
    vmware_cmd_getdevice        = 11,
    vmware_cmd_gethwmodelver    = 17
};
enum vmware_type {
    vmware_type_error = 0,
    vmware_type_esxi = 2,
    vmware_type_workstation = 4
};

static uint32_t check_vmware_version()
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    asm volatile("inl (%%dx)" :
        "=a"(eax), "=c"(ecx), "=d"(edx), "=b"(ebx) :
        "0"(vmware_magic),
        "1"(vmware_cmd_getversion),
        "2"(vmware_port),
        "3"(ebx) :
        "memory");
    if (ebx != vmware_magic)
        return vmware_type_error;
    return ecx;
}

hypervisor_type hypervisor()
{
    if (processor::features().kvm_clocksource || processor::features().kvm_clocksource2) {
        return hypervisor_type::kvm;
    }
    if (processor::features().xen_clocksource) {
        return hypervisor_type::xen;
    }
    switch (check_vmware_version()) {
    case vmware_type_esxi:
        return hypervisor_type::vmware_esxi;
    case vmware_type_workstation:
        return hypervisor_type::vmware_workstation;
    }
    return hypervisor_type::unknown;
}

}
