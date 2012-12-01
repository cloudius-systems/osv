#include "vga.hh"

volatile unsigned short * const VGAConsole::buffer
= reinterpret_cast<volatile unsigned short *>(0xa0000);


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
    _col = 0;
}
