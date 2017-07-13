/*-
 * Copyright (c) 2009-2012,2016 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * Implements low-level interactions with Hypver-V/Azure
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <bsd/porting/netport.h>

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/include/hyperv_busdma.h>
#include <dev/hyperv/vmbus/hyperv_reg.h>
#ifdef OSV_VMBUS_ENABLED // Temporarily disable the code until VMBus based drivers are ported to OSv
#include <dev/hyperv/vmbus/hyperv_machdep.h>
#include <dev/hyperv/vmbus/hyperv_var.h>
#endif

#include <osv/debug.hh>

struct hypercall_ctx {
    void                        *hc_addr;
    struct hyperv_dma   hc_dma;
};

#if 0
static u_int                    hyperv_get_timecount(struct timecounter *);
#endif
#ifdef OSV_VMBUS_ENABLED
static void                     hypercall_memfree();
#endif

u_int                           hyperv_features;
u_int                           hyperv_recommends;

static u_int                    hyperv_pm_features;
static u_int                    hyperv_features3;

hyperv_tc64_t                   hyperv_tc64;

#ifdef OSV_VMBUS_ENABLED
static struct hypercall_ctx     hypercall_context;
#endif

bool
hyperv_is_timecount_available() {
    return (hyperv_features & CPUID_HV_MSR_TIME_REFCNT);
}

uint64_t
hyperv_tc64_rdmsr()
{
    return (processor::rdmsr(MSR_HV_TIME_REF_COUNT));
}

#ifdef OSV_VMBUS_ENABLED // Temporarily disable the code until VMBus based drivers are ported to OSv
uint64_t
hypercall_post_message(bus_addr_t msg_paddr)
{
    return hypercall_md(hypercall_context.hc_addr,
        HYPERCALL_POST_MESSAGE, msg_paddr, 0);
}

uint64_t
hypercall_signal_event(bus_addr_t monprm_paddr)
{
    return hypercall_md(hypercall_context.hc_addr,
        HYPERCALL_SIGNAL_EVENT, monprm_paddr, 0);
}

int
hyperv_guid2str(const struct hyperv_guid *guid, char *buf, size_t sz)
{
    const uint8_t *d = guid->hv_guid;

    return snprintf(buf, sz, "%02x%02x%02x%02x-"
        "%02x%02x-%02x%02x-%02x%02x-"
        "%02x%02x%02x%02x%02x%02x",
        d[3], d[2], d[1], d[0],
        d[5], d[4], d[7], d[6], d[8], d[9],
        d[10], d[11], d[12], d[13], d[14], d[15]);
}
#endif

bool
hyperv_identify()
{
    struct processor::cpuid_result regs;
    unsigned int maxleaf;

    regs = processor::cpuid(CPUID_LEAF_HV_MAXLEAF);
    maxleaf = regs.a;
    if (maxleaf < CPUID_LEAF_HV_LIMITS)
        return (false);

    regs = processor::cpuid(CPUID_LEAF_HV_INTERFACE);
    if (regs.a != CPUID_HV_IFACE_HYPERV)
        return (false);

    regs = processor::cpuid(CPUID_LEAF_HV_FEATURES);
    if ((regs.a & CPUID_HV_MSR_HYPERCALL) == 0) {
        /*
         * Hyper-V w/o Hypercall is impossible; someone
         * is faking Hyper-V.
         */
        return (false);
    }
    hyperv_features = regs.a;
    hyperv_pm_features = regs.c;
    hyperv_features3 = regs.d;

    regs = processor::cpuid(CPUID_LEAF_HV_RECOMMENDS);
    hyperv_recommends = regs.a;

    return (true);
}

#ifdef OSV_VMBUS_ENABLED // Temporarily disable the code until VMBus based drivers are ported to OSv
static void
hypercall_memfree()
{
    hyperv_dmamem_free(&hypercall_context.hc_dma,
        hypercall_context.hc_addr);
    hypercall_context.hc_addr = NULL;
}

void
hypercall_create(void *arg __unused)
{
    uint64_t hc, hc_orig;

    hypercall_context.hc_addr = hyperv_dmamem_alloc(NULL, PAGE_SIZE, 0,
        PAGE_SIZE, &hypercall_context.hc_dma, BUS_DMA_WAITOK);
    if (hypercall_context.hc_addr == NULL) {
        printf("hyperv: Hypercall page allocation failed\n");
        return;
    }

    /* Get the 'reserved' bits, which requires preservation. */
    hc_orig = processor::rdmsr(MSR_HV_HYPERCALL);

    /*
     * Setup the Hypercall page.
     *
     * NOTE: 'reserved' bits MUST be preserved.
     */
    hc = ((hypercall_context.hc_dma.hv_paddr >> PAGE_SHIFT) <<
        MSR_HV_HYPERCALL_PGSHIFT) |
        (hc_orig & MSR_HV_HYPERCALL_RSVD_MASK) |
        MSR_HV_HYPERCALL_ENABLE;
    processor::wrmsr(MSR_HV_HYPERCALL, hc);

    /*
     * Confirm that Hypercall page did get setup.
     */
    hc = processor::rdmsr(MSR_HV_HYPERCALL);
    if ((hc & MSR_HV_HYPERCALL_ENABLE) == 0) {
        printf("hyperv: Hypercall setup failed\n");
        hypercall_memfree();
        /* Can't perform any Hyper-V specific actions */
        return;
    }
    if (bootverbose)
        printf("hyperv: Hypercall created\n");
}
SYSINIT(hypercall_ctor, SI_SUB_DRIVERS, SI_ORDER_FIRST, hypercall_create, NULL);

void
hypercall_destroy(void *arg __unused)
{
    uint64_t hc;

    if (hypercall_context.hc_addr == NULL)
        return;

    /* Disable Hypercall */
    hc = processor::rdmsr(MSR_HV_HYPERCALL);
    processor::wrmsr(MSR_HV_HYPERCALL, (hc & MSR_HV_HYPERCALL_RSVD_MASK));
    hypercall_memfree();

    if (bootverbose)
        printf("hyperv: Hypercall destroyed\n");
}
SYSUNINIT(hypercall_dtor, SI_SUB_DRIVERS, SI_ORDER_FIRST, hypercall_destroy,
    NULL);
#endif