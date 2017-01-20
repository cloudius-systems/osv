/*
 * Copyright (C) 2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/types.h>
#include <xen/interface/xen.h>

#include "arch-dtb.hh"
#include "xen.hh"

shared_info_t *HYPERVISOR_shared_info;

namespace xen {

shared_info_t dummy_info;

}

extern "C" {

void init_xen()
{
    HYPERVISOR_shared_info = nullptr;
    if (dtb_get_vmm_is_xen()) {
        /* set valid pointer just to know we're under Xen.
         * Real shared page will be set up later, when page allocator works.
        */
        HYPERVISOR_shared_info = &xen::dummy_info;
    }
}
}
