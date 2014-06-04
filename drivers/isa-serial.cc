/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "isa-serial.hh"

namespace console {

enum IsaSerialValues {
    // UART registers, offsets to ioport:
    IER_ADDRESS = 1,    // Interrupt Enable Register
    FCR_ADDRESS = 2,    // FIFO Control Register
    LCR_ADDRESS = 3,    // Line Control Register
    MCR_ADDRESS = 4,    // Modem Control Register
    LSR_ADDRESS = 5,    // Line Control Register
    MSR_ADDRESS = 6,    // Modem Status Register
    SCR_ADDRESS = 7,    // Scratch Register
    // When bit 7 (DLAB) of LCR is set to 1, the two registers 0 and 1
    // change their meaning and become two bytes controlling the baud rate
    LCR_DLAB = 0x80,    // Divisor Latch Access Bit in LCR register
    LCR_8BIT = 3,
    DLL_ADDRESS = 0,    // Divisor Latch LSB Register
    DLM_ADDRESS = 1,    // Divisor Latch MSB Register
    // Various bits of the Line Status Register
    LSR_RECEIVE_DATA_READY = 0x1,
    LSR_OVERRUN = 0x2,
    LSR_PARITY_ERROR = 0x4,
    LSR_FRAME_ERROR = 0x8,
    LSR_BREAK_INTERRUPT = 0x10,
    LSR_TRANSMIT_HOLD_EMPTY = 0x20,
    LSR_TRANSMIT_EMPTY = 0x40,
    LSR_FIFO_ERROR = 0x80,
    // Various bits of the Modem Control Register
    MCR_DTR = 0x1,
    MCR_RTS = 0x2,
    MCR_AUX_OUTPUT_1 = 0x4,
    MCR_AUX_OUTPUT_2 = 0x8,
    MCR_LOOPBACK_MODE = 0x16,
};

void IsaSerialConsole::write(const char *str, size_t len)
{
    while (len-- > 0)
        writeByte(*str++);
}

bool IsaSerialConsole::input_ready()
{
    u8 lsr = pci::inb(ioport + LSR_ADDRESS);
    return lsr & LSR_RECEIVE_DATA_READY;
}

char IsaSerialConsole::readch()
{
    u8 lsr;
    char letter;

    do {
        lsr = pci::inb(ioport + LSR_ADDRESS);
    } while (!(lsr & (LSR_RECEIVE_DATA_READY | LSR_OVERRUN | LSR_PARITY_ERROR | LSR_FRAME_ERROR)));

    letter = pci::inb(ioport);

    return letter;
}

void IsaSerialConsole::writeByte(const char letter)
{
    u8 lsr;

    do {
        lsr = pci::inb(ioport + LSR_ADDRESS);
    } while (!(lsr & LSR_TRANSMIT_HOLD_EMPTY));

    pci::outb(letter, ioport);
}

void IsaSerialConsole::reset() {
    // Set the UART speed to to 115,200 bps, This is done by writing 1,0 to
    // Divisor Latch registers, but to access these we need to temporarily
    // set the Divisor Latch Access Bit (DLAB) on the LSR register, because
    // the UART has fewer ports than registers...
    pci::outb(LCR_8BIT | LCR_DLAB, ioport + LCR_ADDRESS);
    pci::outb(1, ioport + DLL_ADDRESS);
    pci::outb(0, ioport + DLM_ADDRESS);
    pci::outb(LCR_8BIT, ioport + LCR_ADDRESS);

    //  interrupt threshold
    pci::outb(0, ioport + FCR_ADDRESS);

    // enable interrupts
    pci::outb(1, ioport + IER_ADDRESS);

    // Most physical UARTs need the MCR AUX_OUTPUT_2 bit set to 1 for
    // interrupts to be generated. QEMU doesn't bother checking this
    // bit, but interestingly VMWare does, so we must set it.
    pci::outb(MCR_AUX_OUTPUT_2, ioport + MCR_ADDRESS);
}

void IsaSerialConsole::dev_start() {
    _irq = new gsi_edge_interrupt(4, [&] { _thread->wake(); });
    reset();
}

}
