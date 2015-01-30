/*
 * Copyright (C) 2014-2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PL011_HH
#define PL011_HH

#include "console-driver.hh"
#include "exceptions.hh"
#include <osv/interrupt.hh>

namespace console {

class PL011_Console : public console_driver {
public:
    virtual void write(const char *str, size_t len);
    virtual void flush();
    virtual bool input_ready();
    virtual char readch();

    void set_base_addr(u64 addr);
    u64 get_base_addr();
    void set_irqid(int irqid);

private:
    virtual void dev_start();
    virtual const char *thread_name() { return "pl011-input"; }
    bool ack_irq();
    void irq_handler();
    /* default UART irq = SPI 1 = 32 + 1 */
    unsigned int irqid = 33;
    std::unique_ptr<spi_interrupt> _irq;
};

}

#endif /* PL011_HH */
