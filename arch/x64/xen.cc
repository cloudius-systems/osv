/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "xen.hh"
#include "debug.hh"
#include "mmu.hh"
#include "processor.hh"
#include "cpuid.hh"
#include "exceptions.hh"
#include "interrupt.hh"
#include <osv/sched.hh>
#include <bsd/porting/pcpu.h>
#include <bsd/machine/xen/xen-os.h>

shared_info_t *HYPERVISOR_shared_info;
uint8_t xen_features[XENFEAT_NR_SUBMAPS * 32];
// make sure xen_start_info is not in .bss, or it will be overwritten
// by init code, as xen_init() is called before .bss initialization
struct start_info* xen_start_info __attribute__((section(".data")));

namespace xen {

// we only have asm constraints for the first three hypercall args,
// so we only inline hypercalls with <= 3 args.  The others are out-of-line,
// implemented in normal asm.
inline ulong hypercall(unsigned type, ulong a1, ulong a2, ulong a3)
{
    ulong ret;
    asm volatile("call *%[hyp]"
                 : "=a"(ret), "+D"(a1), "+S"(a2), "+d"(a3)
                 : [hyp]"c"(hypercall_page + 32 * type)
                 : "memory", "r10", "r8");
    return ret;
}

inline ulong hypercall(unsigned type)
{
    return hypercall(type, 0, 0, 0);
}

inline ulong hypercall(unsigned type, ulong a1)
{
    return hypercall(type, a1, 0, 0);
}

inline ulong hypercall(unsigned type, ulong a1, ulong a2)
{
    return hypercall(type, a1, a2, 0);
}

// some template magic to auto-cast pointers to ulongs in hypercall().
inline ulong cast_pointer(ulong v)
{
    return v;
}

inline ulong cast_pointer(void* p)
{
    return reinterpret_cast<ulong>(p);
}

template <typename... T>
inline ulong
memory_hypercall(unsigned type, T... args)
{
    return hypercall(__HYPERVISOR_memory_op, type, cast_pointer(args)...);
}

template <typename... T>
inline ulong
version_hypercall(unsigned type, T... args)
{
    return hypercall(__HYPERVISOR_xen_version, type, cast_pointer(args)...);
}

inline ulong
hvm_hypercall(unsigned type, struct xen_hvm_param *param)
{
    return hypercall(__HYPERVISOR_hvm_op, type, cast_pointer(param));
}

struct xen_shared_info xen_shared_info __attribute__((aligned(4096)));
extern void* xen_bootstrap_end;

static bool xen_pci_enabled()
{
    u16 magic = processor::inw(0x10);
    if (magic != 0x49d2) {
        return false;
    }

    u8 version = processor::inw(0x12);

    if (version != 0) {
        processor::outw(0xffff, 0x10); // product: experimental
        processor::outl(0, 0x10); // build id: whatever
        u16 _magic = processor::inw(0x10); // just make sure we are not blacklisted
        if (_magic != 0x49d2) {
            return false;
        }
    }
    processor::outw(3 ,0x10); // 2 => NICs, 1 => BLK
    return true;
}

#define HVM_PARAM_CALLBACK_IRQ 0
}

void evtchn_irq_is_legacy(void);

namespace xen {

static void xen_ack_irq()
{
    auto cpu = sched::cpu::current();
    HYPERVISOR_shared_info->vcpu_info[cpu->id].evtchn_upcall_pending = 0; 
}

// For HVMOP_set_param the param vector is comprised of
// the vector number in the low part, and then:
// - all the rest zeroed if we are requesting an ISA irq
// - 1 << 56, if we are requesting a PCI INTx irq, and
// - 2 << 56, if we are requesting a direct callback.

void xen_set_callback()
{
    struct xen_hvm_param xhp;

    xhp.domid = DOMID_SELF;
    xhp.index = HVM_PARAM_CALLBACK_IRQ;

    auto vector = idt.register_interrupt_handler(
        [] {}, // pre_eoi
        [] { xen_ack_irq(); }, // eoi
        [] { xen_handle_irq(); }// handler
    );

    xhp.value = vector | (2ULL << 56);

    if (hvm_hypercall(HVMOP_set_param, &xhp))
        assert(0);
}

gsi_level_interrupt *xen_set_callback(int irqno)
{
    struct xen_hvm_param xhp;

    xhp.domid = DOMID_SELF;
    xhp.index = HVM_PARAM_CALLBACK_IRQ;

    if (irqno >= 16) {
        debug("INTx not supported yet.\n");
        abort();
    }

    auto gsi = new gsi_level_interrupt(
        irqno,
        [] { xen_ack_irq(); },
        [] { xen_handle_irq(); }
    );

    xhp.value = irqno;

    if (hvm_hypercall(HVMOP_set_param, &xhp))
        assert(0);

    return gsi;
}

void xen_init(processor::features_type &features, unsigned base)
{
        // Base + 1 would have given us the version number, it is mostly
        // uninteresting for us now
        auto x = processor::cpuid(base + 2);
        processor::wrmsr(x.b, cast_pointer(&hypercall_page));

        struct xen_feature_info info;
        // To fill up the array used by C code
        for (int i = 0; i < XENFEAT_NR_SUBMAPS; i++) {
            info.submap_idx = i;
            if (version_hypercall(XENVER_get_features, &info) < 0)
                assert(0);
            for (int j = 0; j < 32; j++)
                xen_features[i * 32 + j] = !!(info.submap & 1<<j);
        }
        features.xen_clocksource = xen_features[9] & 1;
        features.xen_vector_callback = xen_features[8] & 1;
        if (!features.xen_vector_callback)
            evtchn_irq_is_legacy();

        struct xen_add_to_physmap map;
        map.domid = DOMID_SELF;
        map.idx = 0;
        map.space = 0;
        map.gpfn = cast_pointer(&xen_shared_info) >> 12;

        // 7 => add to physmap
        if (memory_hypercall(XENMEM_add_to_physmap, &map))
            assert(0);

        features.xen_pci = xen_pci_enabled();
        HYPERVISOR_shared_info = reinterpret_cast<shared_info_t *>(&xen_shared_info);
}

extern "C"
void xen_init(struct start_info* si)
{
    xen_start_info = si;
}
}
