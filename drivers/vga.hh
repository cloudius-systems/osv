#ifndef DRIVERS_VGA_HH
#define DRIVERS_VGA_HH

#include "console.hh"

class VGAConsole : public Console {
public:
    virtual void write(const char *str);
    virtual void newline();
private:
    unsigned _col = 0;
    static const unsigned ncols = 80, nrows = 25;
    static volatile unsigned short * const buffer;
};

#endif
