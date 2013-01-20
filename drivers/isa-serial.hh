#ifndef DRIVERS_ISA_SERIAL_HH
#define DRIVERS_ISA_SERIAL_HH

#include "console.hh"
#include "vga.hh"
#include "drivers/pci.hh"

class IsaSerialConsole : public Console {
public:
	IsaSerialConsole();
    virtual void write(const char *str, size_t len);
    virtual void newline();
private:
    static const u16 ioport = 0x3f8;
    u8 lcr;
    enum IsaSerialValues {
    	LCR_ADDRESS = 0x3,
    	LCR_DIVISOR_LATCH_ACCESS_BIT = 0x7,
    	LCR_DIVISOR_LATCH_ACCESS_BIT_LOW = 0x0,
    	LCR_DIVISOR_LATCH_ACCESS_BIT_HIGH = 0x1,
        BAUD_GEN0_ADDRESS = 0x0,
        BAUD_GEN1_ADDRESS = 0x1,
        LSR_ADDRESS = 0x5,
        LSR_TRANSMIT_HOLD_EMPTY = 0x20,
    };

    void reset();
    void writeByte(const char letter);
};

#endif
