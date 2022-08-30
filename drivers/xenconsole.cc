/*
 * Copyright (C) 2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define XENHVM //FIXME hack to reveal hvm_get_parameter()
#include <bsd/porting/netport.h> /* __dead2 defined here */
#include <bsd/porting/bus.h>
#include <machine/xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/interface/hvm/params.h>
#include <xen/interface/io/console.h>
#include <xen/xen_intr.h>
#include <xen/evtchn.h>
#include <osv/mmio.hh>
#include "xenconsole.hh"

namespace console {

XEN_Console::
XEN_Console::XEN_Console()
    : _interface(0)
     , _evtchn(-1)
     , _irq(0)
     , _pfn(0)
{}

void XEN_Console::handle_intr()
{
    _thread->wake_with_irq_disabled();
}

void XEN_Console::write(const char *str, size_t len) {
    assert(len > 0);
    if (!_interface) {
        HYPERVISOR_console_write(str, len);
        return;
    }

    /* str might be larger then ring, so write it by chunks in this case */
    XENCONS_RING_IDX prod = _interface->out_prod;
    constexpr auto ringsize = sizeof(_interface->out) - 1;
    while (true) {
        XENCONS_RING_IDX cons = _interface->out_cons;
        auto delta = prod - cons;

        if (unlikely(delta > ringsize)) {
            prod = cons; /* ring is corrupted, reset is the best we can do */
            delta = 0;
        }

        size_t c = 0;
        for (; c < std::min(ringsize - delta, len); c++)
            _interface->out[MASK_XENCONS_IDX(prod++, _interface->out)] = *str++;

        _interface->out_prod = prod;
        wmb();

        if (likely(c == len))
            break; /* flush() will do evtchn notification */

        len -= ringsize - delta;
        notify_remote_via_evtchn(_evtchn);

        while (_interface->out_cons != _interface->out_prod)
            cpu_relax(); /* can't sleep here */
    }
}

void XEN_Console::dev_start()
{
    _pfn = hvm_get_parameter(HVM_PARAM_CONSOLE_PFN);
    _evtchn = hvm_get_parameter(HVM_PARAM_CONSOLE_EVTCHN);

    if (!_pfn || !_evtchn)
        throw std::runtime_error("fail to get console params");

     if (bind_caller_port_to_irqhandler(_evtchn, "xenconsole",
            XEN_Console::console_intr,
            static_cast<void*>(this),
            INTR_TYPE_MISC, &_irq) != 0)
        throw std::runtime_error("fail to bind evtchn");

    _interface = (xencons_interface*)mmio_map(_pfn << PAGE_SHIFT, PAGE_SIZE, "xen_console");
}

void XEN_Console::flush()
{
    notify_remote_via_evtchn(_evtchn);
}

bool XEN_Console::input_ready()
{
    return _interface->in_cons != _interface->in_prod;
}

char XEN_Console::readch() {
    XENCONS_RING_IDX cons;
    char c;
    assert(_interface);
    cons = _interface->in_cons;
    c = _interface->in[MASK_XENCONS_IDX(cons, _interface->in)];
    mb();
    _interface->in_cons = cons + 1;
    return c;
}
}
