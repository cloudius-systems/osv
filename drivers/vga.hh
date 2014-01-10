/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_VGA_HH
#define DRIVERS_VGA_HH

#include "console.hh"
#include "sched.hh"
#include <termios.h>

class VGAConsole : public Console {
public:
    explicit VGAConsole(sched::thread* consumer, const termios *tio);
    virtual void write(const char *str, size_t len);
    virtual void newline();
    virtual bool input_ready();
    virtual char readch();
private:
    unsigned _col = 0;
    static const unsigned ncols = 80, nrows = 25;
    static volatile unsigned short * const buffer;
    const termios *_tio;
};

#endif
