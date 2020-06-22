/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_ISA_SERIAL_HH
#define DRIVERS_ISA_SERIAL_HH

#include "isa-serial-base.hh"

namespace console {

class isa_serial_console : public isa_serial_console_base {
public:
    static void early_init();
    static const u16 ioport = 0x3f8;
private:
    std::unique_ptr<gsi_edge_interrupt> _irq;
    virtual void dev_start();
    virtual const char *thread_name() { return "isa-serial-input"; }
};
}

#endif
