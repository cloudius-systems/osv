/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_VGA_HH
#define DRIVERS_VGA_HH

#include "console.hh"
#include <osv/sched.hh>
#include <termios.h>
#include "kbd.hh"
#include "libtsm/libtsm.hh"
#include <queue>

class VGAConsole : public Console {
public:
    explicit VGAConsole(sched::thread* consumer, const termios *tio);
    virtual void write(const char *str, size_t len);
    virtual bool input_ready();
    virtual char readch();
    void draw(const uint32_t c, const struct tsm_screen_attr *attr, unsigned int x, unsigned int y);
    void push_queue(const char *str, size_t len);
private:
    unsigned _col = 0;
    static const unsigned ncols = 80, nrows = 25;
    static volatile unsigned short * const buffer;
    const termios *_tio;
    struct tsm_screen *tsm_screen;
    struct tsm_vte *tsm_vte;
    Keyboard kbd;
    std::queue<char> read_queue;
    unsigned short history[ncols * nrows];
};

#endif
