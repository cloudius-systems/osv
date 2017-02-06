/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/xen.hh>
#include <osv/xen_intr.hh>
#include <bsd/porting/bus.h>
#include <machine/intr_machdep.h>
#include "bitops.h"
#include <osv/debug.hh>

#include <osv/trace.hh>

// beware: this measures time between interrupts
TRACEPOINT(trace_xen_irq, "");
TRACEPOINT(trace_xen_irq_ret, "");
// this measures time spent processing an interrupt
TRACEPOINT(trace_xen_irq_exec, "");
TRACEPOINT(trace_xen_irq_exec_ret, "");

/*FIXME: use xen/evtchn.h when aarch64 supprted */
#if defined(__x86_64__)
void unmask_evtchn(int vector);
int evtchn_from_irq(int irq);
#else
void unmask_evtchn(int port)
{
}

int evtchn_from_irq(int irq)
{
    abort("Not implemented yet");
    return 0; /* keep compiler happy */
}

#endif

namespace xen {

PERCPU(sched::thread *, xen_irq::_thread);

struct xen_irq_handler {
    driver_intr_t handler;
    void *arg;
};
static struct xen_irq_handler xen_allocated_irqs[LONG_BIT * 8];

void xen_irq::register_irq(int evtchn, driver_intr_t handler, void *arg)
{
    xen_allocated_irqs[evtchn] = { handler , arg };
}

void xen_irq::unregister_irq(int evtchn)
{
    xen_allocated_irqs[evtchn] = { nullptr , nullptr };
}

// Note that after this is run, every channel that is pending and is valid for this cpu
// will be masked. Because the update of evtchn_mask is atomic, in case of shared event
// channels only one of them will be present in the final mask. That event channel is
// responsible to set down the pending bit and then umask the event channel after it is
// run
//
// FIXME: We could adopt the strategy of registering event channels in different words
// for different cpus. This way we could reduce contention in those words.
inline unsigned long active_evtchns(unsigned long idx, unsigned long *cpu_mask)
{
    return cpu_mask[idx] & xen_shared_info.evtchn_pending[idx].load() &
                         ~(xen_shared_info.evtchn_mask[idx].load());
}

void xen_irq::do_irq()
{
    unsigned long l1, l2;
    unsigned long l1i, l2i;
    int cpu_id = sched::cpu::current()->id;
    xen_vcpu_info *v = &xen_shared_info.vcpu_info[cpu_id];
    unsigned long cpu_mask[8 * 64];

    if (cpu_id == 0)
        memset(cpu_mask, -1, sizeof(cpu_mask)); // FIXME: Right now, all events bound to cpu0
    else
        memset(cpu_mask, 0, sizeof(cpu_mask));

    trace_xen_irq();
    while (true) {

        sched::thread::wait_until([=, &l1] {
            l1 = v->evtchn_pending_sel.exchange(0, std::memory_order_relaxed);
            return l1;
        });

        trace_xen_irq_ret();
        trace_xen_irq_exec();
        while (l1 != 0) {
            l1i = bsrq(l1);
            l1 &= ~(1ULL << l1i);

            while ((l2 = active_evtchns(l1i, cpu_mask)) != 0) {
                l2i = bsrq(l2);
                unsigned long port = (l1i * LONG_BIT) + l2i;

                // FIXME: It should be possible to mask all channels
                // together, but by doing that I lose interrupts eventually.
                // So let's do it like this now and optmize it later.
                xen_shared_info.evtchn_mask[l1i].fetch_or(1ULL << l2i);
                void *arg = xen_allocated_irqs[port].arg;
                xen_shared_info.evtchn_pending[l1i].fetch_and(~(1ULL << l2i));
                xen_allocated_irqs[port].handler(arg);
                unmask_evtchn(port);
            }
        }
        trace_xen_irq_exec_ret();
        trace_xen_irq();
    }
}

void xen_irq::_cpu_init(sched::cpu *c)
{
    *(_thread.for_cpu(c)) = sched::thread::make([this] { xen_irq::do_irq(); },
            sched::thread::attr().pin(c).name(osv::sprintf("xenirq%d", c->id)));
    (*(_thread.for_cpu(c)))->start();
}

xen_irq::xen_irq()
    : _cpu_notifier([this] { cpu_init(); })
{
}

static xen_irq *xen_irq_handlers;
void xen_handle_irq()
{
    xen_irq_handlers->wake();
}

bool xen_ack_irq()
{
    auto cpu = sched::cpu::current();
    HYPERVISOR_shared_info->vcpu_info[cpu->id].evtchn_upcall_pending = 0;
    return true;
}

static __attribute__((constructor)) void setup_xen_irq()
{
    if (!is_xen()) {
        return;
    }

    xen_irq_handlers = new xen_irq;
}
}

int
intr_add_handler(const char *name, int vector, driver_filter_t filter,
    driver_intr_t handler, void *arg, enum intr_type flags, void **cookiep)
{
    xen::xen_irq::register_irq(evtchn_from_irq(vector), handler, arg);
    if (cookiep) {
        *((unsigned long *)cookiep) = evtchn_from_irq(vector);
    }
    return 0;
}

int
intr_register_source(struct intsrc *isrc)
{
    return 0;
}

void
intr_execute_handlers(struct intsrc *isrc, struct trapframe *frame)
{
    // Make sure we are never called by the BSD code
    abort();
}

struct intsrc *
intr_lookup_source(int vector)
{
    // We could wrap this around a fake struct intsrc, but this is only used as
    // a token for execute handlers. Since we don't support sharing, we will
    // only wrap the vector.
    return reinterpret_cast<struct intsrc *>(vector);
}

int
intr_register_pic(struct pic *pic)
{
    return 0;
}

int
intr_remove_handler(void *cookie)
{
    if (cookie) {
        xen::xen_irq::unregister_irq((long)cookie);
    }
    return 0;
}
