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

class IsaSerialConsole : public console_driver {
public:
    virtual void write(const char *str, size_t len);
    virtual void flush() {}
    virtual bool input_ready() override;
    virtual char readch();
private:
    gsi_edge_interrupt* _irq;
    static const u16 ioport = 0x3f8;

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

    virtual void dev_start();
    void reset();
    static void writeByte(const char letter);
    virtual const char *thread_name() { return "isa-serial-input"; }
};

}

#endif
