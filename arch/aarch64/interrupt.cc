/*
 * Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/interrupt.hh>
#include <osv/sched.hh>

/* Software-Generated Interrupts */
sgi_interrupt::sgi_interrupt(enum ipi_id id, std::function<void ()> handler)
    : interrupt(id, handler)
{
    idt.register_interrupt(this);
}

sgi_interrupt::~sgi_interrupt()
{
    idt.unregister_interrupt(this);
}

void sgi_interrupt::send(sched::cpu* cpu)
{
    gic::gic->send_sgi(gic::sgi_filter::SGI_TARGET_LIST,
                       cpu->arch.smp_idx, get_id());
}

void sgi_interrupt::send_allbutself()
{
    gic::gic->send_sgi(gic::sgi_filter::SGI_TARGET_ALL_BUT_SELF,
                       0, get_id());
}

/* Private Peripheral Interrupts */

ppi_interrupt::ppi_interrupt(gic::irq_type t, unsigned id, std::function<void ()> h)
    : interrupt(id, h), irq_type(t)
{
    gic::gic->set_irq_type(id, t);
    idt.register_interrupt(this);
}

ppi_interrupt::~ppi_interrupt() {
    idt.unregister_interrupt(this);
}

/* Shared Peripheral Interrupts */

spi_interrupt::spi_interrupt(gic::irq_type t, unsigned id, std::function<bool ()> a,
                             std::function<void ()> h)
    : interrupt(id, a, h), irq_type(t)
{
    gic::gic->set_irq_type(id, t);
    idt.register_interrupt(this);
}

spi_interrupt::~spi_interrupt() {
    idt.unregister_interrupt(this);
}
