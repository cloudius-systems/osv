#include "xen.hh"
#include "xen_intr.hh"
#define _KERNEL
#include <bsd/porting/bus.h>
#include <machine/intr_machdep.h>

namespace xen {
void xen_irq::do_irq(void)
{
    // Look out for races
    while (true) {
        sched::thread::wait_until([this] {
            return this->_irq_pending != 0;
        });
        _handler(_args);
        _irq_pending--;
    }
}

xen_irq::xen_irq(driver_intr_t handler, void *args)
    : _handler(handler), _args(args), _irq_pending(0)
{
    _thread = new sched::thread([this] { this->do_irq(); });
    _thread->start();
}

xen_irq::~xen_irq()
{
}
}

static xen::xen_irq *xen_allocated_irqs[256];

int
intr_add_handler(const char *name, int vector, driver_filter_t filter,
    driver_intr_t handler, void *arg, enum intr_type flags, void **cookiep)
{

    xen_allocated_irqs[vector] = new xen::xen_irq(handler, arg);
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
    xen_allocated_irqs[vector]->wake();
}

struct intsrc *
intr_lookup_source(int vector)
{
    // We could wrap this around a fake struct intsrc, but this is
    // only used as a token for execute handlers. Since we don't support
    // sharing, we will only wrap the vector.
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
    return 0;
}
