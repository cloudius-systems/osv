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
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/interface/version.h>
#include <xen/interface/hvm/hvm_op.h>
#include <osv/alternative.hh>
#include <osv/interrupt.hh>

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
    arch_vcpu_info_t arch;
    pvclock_vcpu_time_info time;
};

struct xen_shared_info {
    struct xen_vcpu_info vcpu_info[MAX_VIRT_CPUS];

    std::atomic<unsigned long> evtchn_pending[sizeof(unsigned long) * 8];
    std::atomic<unsigned long> evtchn_mask[sizeof(unsigned long) * 8];

    pvclock_wall_clock wc;

    unsigned long pad1[3];
};


namespace xen {

extern struct xen_shared_info xen_shared_info;

void xen_set_callback();
void xen_handle_irq();
bool xen_ack_irq();
void irq_setup(interrupt *intr);
void irq_init();
}


#endif /* XEN_HH*/
