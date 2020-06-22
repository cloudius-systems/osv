/*
 * Copyright (C) 2020 Waldemar Kozaczuk.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "isa-serial-base.hh"

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

void isa_serial_console_base::common_early_init()
{
    // Set the UART speed to to 115,200 bps, This is done by writing 1,0 to
    // Divisor Latch registers, but to access these we need to temporarily
    // set the Divisor Latch Access Bit (DLAB) on the LSR register, because
    // the UART has fewer ports than registers...
    write_byte(lcr::LEN_8BIT | lcr::DLAB, regs::LCR);
    write_byte(1, regs::DLL);
    write_byte(0, regs::DLM);
    write_byte(lcr::LEN_8BIT, regs::LCR);

    //  interrupt threshold
    write_byte(0, regs::FCR);

    // disable interrupts
    write_byte(0, regs::IER);

    // Most physical UARTs need the MCR AUX_OUTPUT_2 bit set to 1 for
    // interrupts to be generated. QEMU doesn't bother checking this
    // bit, but interestingly VMWare does, so we must set it.
    write_byte(mcr::AUX_OUTPUT_2, regs::MCR);
}

void isa_serial_console_base::write(const char *str, size_t len)
{
    while (len-- > 0)
        putchar(*str++);
}

bool isa_serial_console_base::input_ready()
{
    u8 val = read_byte(regs::LSR);
    // On VMWare hosts without a serial port, this register always
    // returns 0xff.  Just ignore it instead of spinning incessantly.
    return (val != 0xff && (val & lsr::RECEIVE_DATA_READY));
}

char isa_serial_console_base::readch()
{
    u8 val;
    char letter;

    do {
        val = read_byte(regs::LSR);
    } while (!(val & (lsr::RECEIVE_DATA_READY | lsr::OVERRUN | lsr::PARITY_ERROR | lsr::FRAME_ERROR)));

    letter = read_byte(0);

    return letter;
}

void isa_serial_console_base::putchar(const char ch)
{
    u8 val;

    do {
        val = read_byte(regs::LSR);
    } while (!(val & lsr::TRANSMIT_HOLD_EMPTY));

    write_byte(ch, 0);
}

void isa_serial_console_base::enable_interrupt()
{
    // enable interrupts
    write_byte(1, regs::IER);
}

}
