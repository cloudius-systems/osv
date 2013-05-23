#include "isa-serial.hh"
#include <string.h>

IsaSerialConsole::IsaSerialConsole(sched::thread* poll_thread)
    : _irq(4, [=] { poll_thread->wake(); })
{
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
    // Set the UART speed to to 115,200 bps, This is done by writing 1,0 to
    // Divisor Latch registers, but to access these we need to temporarily
    // set the Divisor Latch Access Bit (DLAB) on the LSR register, because
    // the UART has fewer ports than registers...
    lcr = pci::inb(ioport + LCR_ADDRESS);
    pci::outb(lcr | LCR_DLAB, ioport + LCR_ADDRESS);
    pci::outb(1, ioport + DLL_ADDRESS);
    pci::outb(0, ioport + DLM_ADDRESS);
    pci::outb(lcr & ~LCR_DLAB, ioport + LCR_ADDRESS);

    //  interrupt threshold
    pci::outb(0, ioport + FCR_ADDRESS);

    // enable interrupts
    pci::outb(1, ioport + IER_ADDRESS);
}
