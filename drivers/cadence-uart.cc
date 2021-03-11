/*
 * Copyright (C) 2021 DornerWorks, Ltd
 * Author: Stewart Hildebrand
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "cadence-uart.hh"
#include <stdint.h>

namespace console {

// Register reference:
// https://www.xilinx.com/html_docs/registers/ug1087/mod___uart.html

#define BIT(x) (1U << (x))

#define UART_CR_RXRES       BIT(0)  // RX reset
#define UART_CR_TXRES       BIT(1)  // TX reset
#define UART_CR_RXEN        BIT(2)  // RX enable
#define UART_CR_TXEN        BIT(4)  // TX enable

#define UART_MR_PARITY_NONE BIT(5)  // No parity

#define UART_SR_RTRIG       BIT(0)  // RX trigger
#define UART_SR_REMPTY      BIT(1)  // RX empty
#define UART_SR_TEMPTY      BIT(3)  // TX empty
#define UART_SR_TFUL        BIT(4)  // TX full
#define UART_SR_TACTIVE     BIT(11) // TX active

typedef struct __attribute__ ((aligned (4))) {
    volatile uint32_t cr;    // 0x00 Control register
    volatile uint32_t mr;    // 0x04 Mode register
    volatile uint32_t ier;   // 0x08 Interrupt enable register
    volatile uint32_t idr;   // 0x0C Interrupt disable register
    volatile uint32_t imr;   // 0x10 Interrupt mask register
    volatile uint32_t cisr;  // 0x14 Channel interrupt status register
    uint32_t pad1[2];        // 0x18 - 0x1C
    volatile uint32_t rtrig; // 0x20 RX trigger threshold
    uint32_t pad2[2];        // 0x24 - 0x28
    volatile uint32_t sr;    // 0x2C Status register
    volatile uint32_t fifo;  // 0x30 RX/TX FIFO register
} cadence_t;

static_assert(sizeof(cadence_t) == 0x34, "Wrong size for cadence_t");

// Default base addr
cadence_t *uart = (cadence_t *)0xff000000;

bool Cadence_Console::active = false;

void Cadence_Console::set_base_addr(u64 addr)
{
    // Intentional bitwise AND inside condition
    if (addr & 0x3U) {
        abort("UART base address is not 32-bit aligned");
    }
    uart = (cadence_t *)addr;
}

void Cadence_Console::set_irqid(int irqid) {
    this->irqid = irqid;
}

u64 Cadence_Console::get_base_addr() {
    return (u64)uart;
}

void Cadence_Console::flush()
{
    uint32_t sr;
    do {
        sr = uart->sr;
        asm volatile("nop");
        // Intentional bitwise AND inside condition
    } while (!(sr & UART_SR_TEMPTY) || (sr & UART_SR_TACTIVE));
}

bool Cadence_Console::input_ready() {
    return _input_ready;
}

char Cadence_Console::readch()
{
    _input_ready = false;
    return _uart_fifo;
}

bool Cadence_Console::ack_irq()
{
    // Intentional bitwise AND inside condition
    if (uart->cisr & uart->imr) {
        return true;
    }
    return false;
}

void Cadence_Console::irq_handler()
{
    uint32_t cisr = uart->cisr & uart->imr;

    // Intentional bitwise AND inside condition
    if ((cisr & UART_SR_RTRIG) && !(uart->sr & UART_SR_REMPTY)) {
        _uart_fifo = uart->fifo;
        _input_ready = true;
    }

    // IRQ must be cleared after character is read from FIFO
    uart->cisr = cisr;

    _thread->wake();
}

void Cadence_Console::dev_start() {
    flush();
    // Reset and enable the RX and TX paths
    uart->cr = UART_CR_RXRES | UART_CR_TXRES | UART_CR_RXEN | UART_CR_TXEN;

    uart->mr = UART_MR_PARITY_NONE;

    _irq.reset(new spi_interrupt(gic::irq_type::IRQ_TYPE_LEVEL, this->irqid,
                                 [this] { return this->ack_irq(); },
                                 [this] { this->irq_handler(); }));

    uart->rtrig = 1U;
    uart->cisr = ~0U;
    uart->idr = ~0U;

    uart->ier = UART_SR_RTRIG;
}

void Cadence_Console::write(const char *str, size_t len) {
    while (len > 0) {
        // Intentional bitwise AND inside condition
        while (uart->sr & UART_SR_TFUL) {
            // spin
            asm volatile("nop");
        }
        uart->fifo = *str++;
        len--;
    }
}

}
