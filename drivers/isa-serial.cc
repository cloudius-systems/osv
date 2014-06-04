/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "isa-serial.hh"

namespace console {

// UART registers, offsets to ioport:
enum regs {
    IER = 1,    // Interrupt Enable Register
    FCR = 2,    // FIFO Control Register
    LCR = 3,    // Line Control Register
    MCR = 4,    // Modem Control Register
    LSR = 5,    // Line Control Register
    MSR = 6,    // Modem Status Register
    SCR = 7,    // Scratch Register
    DLL = 0,    // Divisor Latch LSB Register
    DLM = 1,    // Divisor Latch MSB Register
};

enum lcr {
    // When bit 7 (DLAB) of LCR is set to 1, the two registers 0 and 1
    // change their meaning and become two bytes controlling the baud rate
    DLAB     = 0x80,    // Divisor Latch Access Bit in LCR register
    LEN_8BIT = 3,
};

// Various bits of the Line Status Register
enum lsr {
    RECEIVE_DATA_READY  = 0x1,
    OVERRUN             = 0x2,
    PARITY_ERROR        = 0x4,
    FRAME_ERROR         = 0x8,
    BREAK_INTERRUPT     = 0x10,
    TRANSMIT_HOLD_EMPTY = 0x20,
    TRANSMIT_EMPTY      = 0x40,
    FIFO_ERROR          = 0x80,
};

// Various bits of the Modem Control Register
enum mcr {
    DTR                 = 0x1,
    RTS                 = 0x2,
    AUX_OUTPUT_1        = 0x4,
    AUX_OUTPUT_2        = 0x8,
    LOOPBACK_MODE       = 0x16,
};

void IsaSerialConsole::write(const char *str, size_t len)
{
    while (len-- > 0)
        writeByte(*str++);
}

bool IsaSerialConsole::input_ready()
{
    u8 val = pci::inb(ioport + regs::LSR);
    return val & lsr::RECEIVE_DATA_READY;
}

char IsaSerialConsole::readch()
{
    u8 val;
    char letter;

    do {
        val = pci::inb(ioport + regs::LSR);
    } while (!(val & (lsr::RECEIVE_DATA_READY | lsr::OVERRUN | lsr::PARITY_ERROR | lsr::FRAME_ERROR)));

    letter = pci::inb(ioport);

    return letter;
}

void IsaSerialConsole::writeByte(const char letter)
{
    u8 val;

    do {
        val = pci::inb(ioport + regs::LSR);
    } while (!(val & lsr::TRANSMIT_HOLD_EMPTY));

    pci::outb(letter, ioport);
}

void IsaSerialConsole::reset() {
    // Set the UART speed to to 115,200 bps, This is done by writing 1,0 to
    // Divisor Latch registers, but to access these we need to temporarily
    // set the Divisor Latch Access Bit (DLAB) on the LSR register, because
    // the UART has fewer ports than registers...
    pci::outb(lcr::LEN_8BIT | lcr::DLAB, ioport + regs::LCR);
    pci::outb(1, ioport + regs::DLL);
    pci::outb(0, ioport + regs::DLM);
    pci::outb(lcr::LEN_8BIT, ioport + regs::LCR);

    //  interrupt threshold
    pci::outb(0, ioport + regs::FCR);

    // enable interrupts
    pci::outb(1, ioport + regs::IER);

    // Most physical UARTs need the MCR AUX_OUTPUT_2 bit set to 1 for
    // interrupts to be generated. QEMU doesn't bother checking this
    // bit, but interestingly VMWare does, so we must set it.
    pci::outb(mcr::AUX_OUTPUT_2, ioport + regs::MCR);
}

void IsaSerialConsole::dev_start() {
    _irq = new gsi_edge_interrupt(4, [&] { _thread->wake(); });
    reset();
}

}
