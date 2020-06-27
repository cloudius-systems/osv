/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_MMIO_ISA_SERIAL_HH
#define DRIVERS_MMIO_ISA_SERIAL_HH

#include "isa-serial-base.hh"

namespace console {

class mmio_isa_serial_console : public isa_serial_console_base {
public:
    void set_irqid(int irqid) { this->irqid = irqid; }
    static void early_init(u64 mmio_phys_address);
    static void memory_map();
    static void clean_cmdline(char *cmdline);
    static mmioaddr_t _addr_mmio;
    static u64 _phys_mmio_address;
private:
    unsigned int irqid;
    std::unique_ptr<spi_interrupt> _irq;
    virtual void dev_start();
    virtual const char *thread_name() { return "mmio-isa-serial-input"; }
};

}

#endif
