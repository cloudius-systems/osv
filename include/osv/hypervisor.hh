/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_HYPERVISOR_HH
#define OSV_HYPERVISOR_HH

namespace osv {

enum class hypervisor_type {
    unknown,
    kvm,
    xen,
    vmware_workstation,
    vmware_esxi
};

hypervisor_type hypervisor();

}

#endif
