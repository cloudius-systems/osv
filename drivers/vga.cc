/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "vga.hh"

volatile unsigned short * const VGAConsole::buffer
= reinterpret_cast<volatile unsigned short *>(0xb8000);


void VGAConsole::write(const char *str)
{
    while (*str) {
	buffer[(nrows-1)*ncols + _col++] = 0x700 + *str++;
	if (_col == ncols) {
	    newline();
	}
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
