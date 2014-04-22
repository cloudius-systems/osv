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
    void move_cursor(unsigned int x, unsigned int y);
    void update_offset(int scroll_count);
    void apply_offset();
    void push_queue(const char *str, size_t len);
private:
    enum {
        VGA_CRT_IC = 0x3d4,
        VGA_CRT_DC = 0x3d5,
        VGA_CRTC_START_HI = 0x0c,
        VGA_CRTC_START_LO = 0x0d,
        VGA_CRTC_CURSOR_HI = 0x0e,
        VGA_CRTC_CURSOR_LO = 0x0f,
        BUFFER_SIZE = 0x4000,
        OFFSET_LIMIT = 178,
        NCOLS = 80,
        NROWS = 25
    };
    unsigned _col = 0;
    static volatile unsigned short * const _buffer;
    const termios *_tio;
    struct tsm_screen *_tsm_screen;
    struct tsm_vte *_tsm_vte;
    Keyboard _kbd;
    std::queue<char> _read_queue;
    unsigned short _history[BUFFER_SIZE];
    unsigned _offset;
    bool _offset_dirty;
};

#endif
