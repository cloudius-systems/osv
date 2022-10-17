/*
 * Copyright (C) 2014-2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 */

#include "pl011.hh"

/* spec: see PrimeCell UART (PL011) Technical Reference Manual.
 * implemented according to Revision: r1p5.
 */

namespace console {

/* default base addr */
static volatile char *uart = (char *)0x9000000;

enum {
    UARTDR    = 0x000,
    UARTFR    = 0x018,
    UARTIMSC  = 0x038,
    UARTMIS   = 0x040,
    UARTICR   = 0x044
};

bool PL011_Console::active = false;

void PL011_Console::set_base_addr(u64 addr)
{
    uart = (char *)addr;
}

void PL011_Console::set_irqid(int irqid)
{
    this->irqid = irqid;
}

u64 PL011_Console::get_base_addr()
{
    return (u64)uart;
}

void PL011_Console::flush() {
    return;
}

bool PL011_Console::input_ready() {
    /* Check if Receive FIFO is not empty */
    return !(uart[UARTFR] & 0x10); /* RXFE */
}

char PL011_Console::readch() {
    return uart[UARTDR];
}

bool PL011_Console::ack_irq() {
    /* check Masked Interrupt Status Register for UARTRXINTR */
    if (uart[UARTMIS] & 0x10) {
        /* Interrupt Clear Register, clear UARTRXINTR */
        uart[UARTICR] = 0x10;
        return true;
    }
    return false;
}

void PL011_Console::irq_handler() {
    _thread->wake_with_irq_disabled();
}

void PL011_Console::dev_start() {
    /* trigger interrupt on Receive */
    uart[UARTIMSC] = 0x10; /* UARTRXINTR */

    /* UART irq = SPI 1 = 32 + 1 */
    _irq.reset(new spi_interrupt(gic::irq_type::IRQ_TYPE_EDGE, this->irqid,
                                 [this] { return this->ack_irq(); },
                                 [this] { this->irq_handler(); }));
}

void PL011_Console::write(const char *str, size_t len) {
    while (len > 0) {
        uart[UARTDR] = *str++;
        len--;
    }
}

}
