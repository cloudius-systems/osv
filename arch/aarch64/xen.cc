/*
 * Copyright (C) 2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define CONF_drivers_xen 1
#include <osv/types.h>
#include <osv/xen.hh>
#include <xen/interface/xen.h>
#include <xen/features.h>
#include <bsd/porting/netport.h> /* __dead2 defined here */
#include <machine/xen/xen-os.h>
#include <xen/evtchn.h>
#include <machine/xen/hypercall.h>

#include "arch-dtb.hh"
#include "cpuid.hh"

shared_info_t *HYPERVISOR_shared_info;
uint8_t xen_features[XENFEAT_NR_SUBMAPS * 32];

namespace xen {

struct xen_shared_info xen_shared_info __attribute__((aligned(PAGE_SIZE)));
constexpr int events_irq = 31; /*FIXME: get from FDT */

/*TODO: this can be common x64/aarch64 code */
void get_features(processor::features_type &features)
{
    if (!is_xen())
        return;

    for (unsigned int i = 0; i < XENFEAT_NR_SUBMAPS; i++) {
        struct xen_feature_info info = {
            .submap_idx = i,
        };

        if (HYPERVISOR_xen_version(XENVER_get_features, &info) < 0)
            assert(0);

        for (int j = 0; j < 32; j++)
            xen_features[i * 32 + j] = !!(info.submap & 1<<j);
    }

    features.xen_clocksource = xen_feature(XENFEAT_hvm_safe_pvclock);
    features.xen_vector_callback = xen_feature(XENFEAT_hvm_callback_vector);

    if (!processor::features().xen_vector_callback)
        evtchn_irq_is_legacy();
}

void setup()
{
   struct xen_add_to_physmap map = {
        .domid = DOMID_SELF,
        .space = XENMAPSPACE_shared_info,
        .idx = 0,
        .gpfn = reinterpret_cast<xen_pfn_t>(&xen_shared_info) >> PAGE_SHIFT,
    };

    if (HYPERVISOR_memory_op(XENMEM_add_to_physmap, &map))
        assert(0);

    HYPERVISOR_shared_info = reinterpret_cast<shared_info_t *>(&xen_shared_info);
}

void irq_init()
{
    if (!is_xen())
        return;

    evtchn_init(NULL);

    auto intr = new spi_interrupt(gic::irq_type::IRQ_TYPE_LEVEL, events_irq,
                                  xen::xen_ack_irq,
                                  xen::xen_handle_irq);
    irq_setup(intr);
}

}

extern "C" {

void init_xen()
{
    HYPERVISOR_shared_info = nullptr;
    if (dtb_get_vmm_is_xen())
        xen::setup();
}

}
