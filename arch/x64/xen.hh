/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef XEN_HH
#define XEN_HH
#include <atomic>
#include <osv/types.h>
#include <osv/pvclock-abi.hh>
#include "cpuid.hh"
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/interface/version.h>
#include <xen/interface/hvm/hvm_op.h>
#include "alternative.hh"

extern char hypercall_page[];
extern uint8_t xen_features[];
extern struct start_info* xen_start_info;
extern "C" shared_info_t *HYPERVISOR_shared_info;

#define XENPV_ALTERNATIVE(x, y) ALTERNATIVE((xen_start_info != nullptr), x, y)
#define is_xen() (HYPERVISOR_shared_info != nullptr)

// We don't support 32 bit
struct xen_vcpu_info {
    uint8_t evtchn_upcall_pending;
    uint8_t evtchn_upcall_mask;
    std::atomic<uint64_t> evtchn_pending_sel;
    uint64_t cr2;
    uint64_t pad;
    pvclock_vcpu_time_info time;
};

struct xen_shared_info {
    struct xen_vcpu_info vcpu_info[32];

    std::atomic<unsigned long> evtchn_pending[sizeof(unsigned long) * 8];
    std::atomic<unsigned long> evtchn_mask[sizeof(unsigned long) * 8];

    pvclock_wall_clock wc;

    unsigned long pad1[3];
};

class gsi_level_interrupt;

namespace xen {

void xen_init(processor::features_type &features, unsigned base);
extern struct xen_shared_info xen_shared_info;
gsi_level_interrupt *xen_set_callback(int irqno);
void xen_set_callback();

}

#endif
