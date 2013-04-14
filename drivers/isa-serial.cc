#include "isa-serial.hh"
#include <string.h>

IsaSerialConsole::IsaSerialConsole() {
	reset();
}

void IsaSerialConsole::write(const char *str, size_t len)
{
    while (len > 0) {
    	writeByte(*str++);
        len--;
    }
}

bool IsaSerialConsole::input_ready()
{
    u8 lsr = pci::inb(ioport + LSR_ADDRESS);
    return (lsr & LSR_RECEIVE_DATA_READY);
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

void IsaSerialConsole::newline()
{
	writeByte('\n');
	writeByte('\r');
}

void IsaSerialConsole::reset() {
	// Put the line control register in acceptance mode (latch)
	lcr = pci::inb(ioport + LCR_ADDRESS);
	lcr |= 1 << LCR_DIVISOR_LATCH_ACCESS_BIT & LCR_DIVISOR_LATCH_ACCESS_BIT_HIGH;
    pci::outb(lcr, ioport + LCR_ADDRESS);

    pci::outb(1, ioport + BAUD_GEN0_ADDRESS);
    pci::outb(0, ioport + BAUD_GEN1_ADDRESS);

	// Close the latch
	lcr = pci::inb(ioport + LCR_ADDRESS);
	lcr &= ~(1 << LCR_DIVISOR_LATCH_ACCESS_BIT & LCR_DIVISOR_LATCH_ACCESS_BIT_HIGH);
    pci::outb(lcr, ioport + LCR_ADDRESS);
}
