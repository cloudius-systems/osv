#include "xen.hh"
#include "xen_intr.hh"
#define _KERNEL
#include <bsd/porting/bus.h>
#include <machine/intr_machdep.h>

extern "C" {
    void unmask_evtchn(int vector);
    int evtchn_from_irq(int irq);
}

namespace xen {
void xen_irq::do_irq(void)
{
    while (true) {
        sched::thread::wait_until([this] {
            return _irq_pending.exchange(true, std::memory_order_relaxed);
        });

        _handler(_args);
        unmask_evtchn(_evtchn);
    }
}

xen_irq::xen_irq(driver_intr_t handler, int evtchn, void *args)
    : _handler(handler),  _thread([this] { do_irq(); }), _args(args),
    _irq_pending(0), _evtchn(evtchn)
{
    _thread.start();
}
}

static xen::xen_irq *xen_allocated_irqs[256];

int
intr_add_handler(const char *name, int vector, driver_filter_t filter,
    driver_intr_t handler, void *arg, enum intr_type flags, void **cookiep)
{

    assert(!xen_allocated_irqs[vector]);
    xen_allocated_irqs[vector] = new xen::xen_irq(handler, evtchn_from_irq(vector), arg);
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
    int vector = reinterpret_cast<long>(isrc);
    assert(vector < 256);
    assert(xen_allocated_irqs[vector]);

    // FIXME: Technically, this should be under a lock. RCU is perfect, since
    // locking read-side is useless and expensive. However, for now we are fine
    // without it: We are not doing irq sharing, and not supporting
    // unregistering.  Therefore once we register, this pointer should be
    // stable. We never call this function before we register as well (and if
    // we do, the assert above will trigger)
    xen_allocated_irqs[vector]->wake();
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
    // Not yet
    abort();
    return 0;
}
