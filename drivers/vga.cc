/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "vga.hh"
#include <osv/mmu.hh>

volatile unsigned short * const VGAConsole::buffer
= reinterpret_cast<volatile unsigned short *>(mmu::phys_mem + 0xb8000);

VGAConsole::VGAConsole(sched::thread* poll_thread, const termios *tio)
    : _tio(tio)
{
}

void VGAConsole::write(const char *str, size_t len)
{
    while (len > 0) {
        if ((*str == '\n') && (_tio->c_oflag & OPOST)
            && (_tio->c_oflag & ONLCR))
            newline();
        else {
            buffer[(nrows-1)*ncols + _col++] = 0x700 + *str;
            if (_col == ncols)
                newline();
        }
        str++;
        len--;
    }
}

void VGAConsole::newline()
{
    for (unsigned row = 0; row < nrows - 1; ++row) {
	for (unsigned col = 0; col < ncols; ++col) {
	    buffer[row*ncols+col] = buffer[(row+1)*ncols+col];
	}
    }
    for (unsigned col = 0; col < ncols; ++col) {
	buffer[(nrows - 1) * ncols + col] = 0x700;
    }
    _col = 0;
}

bool VGAConsole::input_ready()
{
    return false;
}

char VGAConsole::readch()
{
    return 0;
}
