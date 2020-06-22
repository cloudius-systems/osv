/*
 * Copyright (C) 2020 Waldemar Kozaczuk.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_ISA_SERIAL_BASE_HH
#define DRIVERS_ISA_SERIAL_BASE_HH

#include "console-driver.hh"
#include <osv/pci.hh>
#include <osv/sched.hh>
#include <osv/interrupt.hh>

namespace console {

class isa_serial_console_base : public console_driver {
public:
    virtual void write(const char *str, size_t len);
    virtual void flush() {}
    virtual bool input_ready() override;
    virtual char readch();
protected:
    static void common_early_init();
    static u8 read_byte(int);
    static void write_byte(u8, int);
    void enable_interrupt();
private:
    void putchar(const char ch);
};
}

#endif
