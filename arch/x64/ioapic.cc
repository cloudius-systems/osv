/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/interrupt.hh>
#include "exceptions.hh"
#include <osv/mmu.hh>
#include <osv/mutex.h>

namespace ioapic {

constexpr u64 base_phys = 0xfec00000;
volatile void* const base = mmu::phys_cast<void>(0xfec00000);
constexpr unsigned index_reg_offset = 0;
constexpr unsigned data_reg_offset = 0x10;

mutex mtx;

volatile u32* index_reg()
{
    return static_cast<volatile u32*>(base + index_reg_offset);
}

volatile u32* data_reg()
{
    return static_cast<volatile u32*>(base + data_reg_offset);
}

u32 read(unsigned reg)
{
    *index_reg() = reg;
    return *data_reg();
}

void write(unsigned reg, u32 data)
{
    *index_reg() = reg;
    *data_reg() = data;
}

void init()
{
    mmu::linear_map(const_cast<void*>(base), base_phys, 4096);
}

}

using namespace ioapic;

void gsi_interrupt::set(unsigned gsi, unsigned vector)
{
    WITH_LOCK(mtx) {
        write(0x10 + gsi * 2 + 1, sched::cpus[0]->arch.apic_id << 24);
        write(0x10 + gsi * 2, vector);
    }
    _gsi = gsi;
}

void gsi_interrupt::clear()
{
    WITH_LOCK(mtx) {
        write(0x10 + _gsi * 2, 1 << 16);  // mask
    }
}

gsi_edge_interrupt::gsi_edge_interrupt(unsigned gsi,
                                       std::function<void ()> handler)
    : _vector(idt.register_handler(handler))
{
    _gsi.set(gsi, _vector);
}

gsi_edge_interrupt::~gsi_edge_interrupt()
{
    _gsi.clear();
    idt.unregister_handler(_vector);
}

gsi_level_interrupt::gsi_level_interrupt(unsigned gsi,
                                         std::function<bool ()> ack,
                                         std::function<void ()> handler)
    : _vector(idt.register_level_triggered_handler(gsi, ack, handler))
{
    _gsi.set(gsi, _vector);
}

gsi_level_interrupt::~gsi_level_interrupt()
{
    _gsi.clear();
    idt.unregister_handler(_vector);
}

