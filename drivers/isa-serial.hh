#ifndef DRIVERS_ISA_SERIAL_HH
#define DRIVERS_ISA_SERIAL_HH

#include "console.hh"
#include "vga.hh"
#include "drivers/pci.hh"
#include "sched.hh"
#include "interrupt.hh"

class IsaSerialConsole : public Console {
public:
	explicit IsaSerialConsole(sched::thread* consumer);
    virtual void write(const char *str, size_t len);
    virtual bool input_ready() override;
    virtual char readch();
private:
    gsi_edge_interrupt _irq;
    static const u16 ioport = 0x3f8;
    u8 lcr;
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
    };

    void reset();
    void writeByte(const char letter);
};

#endif
