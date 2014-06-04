/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_ISA_SERIAL_HH
#define DRIVERS_ISA_SERIAL_HH

#include "console-driver.hh"
#include "drivers/pci.hh"
#include <osv/sched.hh>
#include <osv/interrupt.hh>

namespace console {

class isa_serial_console : public console_driver {
public:
    virtual void write(const char *str, size_t len);
    virtual void flush() {}
    virtual bool input_ready() override;
    virtual char readch();
private:
    gsi_edge_interrupt* _irq;
    static const u16 ioport = 0x3f8;

    virtual void dev_start();
    void reset();
    static void putchar(const char ch);
    virtual const char *thread_name() { return "isa-serial-input"; }
};

}

#endif
