/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/interrupt.hh>
#include "apic.hh"

using namespace processor;

inter_processor_interrupt::inter_processor_interrupt(enum ipi_id ipi_id, std::function<void ()> handler)
    : interrupt(ipi_id, handler)
{
    idt.register_interrupt(this);
}

inter_processor_interrupt::~inter_processor_interrupt()
{
    idt.unregister_interrupt(this);
}

void inter_processor_interrupt::send(sched::cpu* cpu)
{
    apic->ipi(cpu->arch.apic_id, _vector);
}

void inter_processor_interrupt::send_allbutself()
{
    apic->ipi_allbutself(_vector);
}

void inter_processor_interrupt::set_vector(unsigned v)
{
    _vector = v;
}

unsigned inter_processor_interrupt::get_vector()
{
    return _vector;
}

gsi_edge_interrupt::gsi_edge_interrupt(unsigned id, std::function<void ()> h)
    : interrupt(id, h)
{
    idt.register_interrupt(this);
}

gsi_edge_interrupt::~gsi_edge_interrupt()
{
    idt.unregister_interrupt(this);
}

void gsi_edge_interrupt::set_vector(unsigned v)
{
    _vector = v;
}

unsigned gsi_edge_interrupt::get_vector()
{
    return _vector;
}

void gsi_edge_interrupt::enable()
{
    _gsi.set(get_id(), _vector);
}

void gsi_edge_interrupt::disable()
{
    _gsi.clear();
}

gsi_level_interrupt::gsi_level_interrupt(unsigned id, std::function<bool ()> a,
                                         std::function<void ()> h)
    : interrupt(id, a, h), _vector(0, 0)
{
    idt.register_interrupt(this);
}

gsi_level_interrupt::~gsi_level_interrupt()
{
    idt.unregister_interrupt(this);
}

void gsi_level_interrupt::set_vector(shared_vector v)
{
    _vector = v;
}

shared_vector gsi_level_interrupt::get_vector()
{
    return _vector;
}

void gsi_level_interrupt::enable()
{
    _gsi.set(get_id(), _vector.vector);
}

void gsi_level_interrupt::disable()
{
    _gsi.clear();
}
