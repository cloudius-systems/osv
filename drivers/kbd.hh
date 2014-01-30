/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_KBD_HH
#define DRIVERS_KBD_HH

#include "console.hh"
#include <osv/interrupt.hh>
#include <termios.h>

enum modifiers {
    MOD_SHIFT = 1<<0,
    MOD_CTL = 1<<1,
    MOD_ALT = 1<<2,
    MOD_CAPSLOCK = 1<<3,
    MOD_NUMLOCK = 1<<4,
    MOD_SCROLLLOCK = 1<<5,
    MOD_E0ESC = 1<<6,
};

enum special_keycode {
    KEY_Escape = '\x1b',
    KEY_Up = 0x80,
    KEY_Down = 0x81,
    KEY_Page_Up = 0x82,
    KEY_Page_Down = 0x83,
    KEY_Left = 0x84,
    KEY_Right = 0x85,
    KEY_Home = 0x86,
    KEY_End = 0x87,
    KEY_Insert = 0x88,
    KEY_Delete = 0x89,
};

class Keyboard {
public:
    explicit Keyboard(sched::thread* consumer);
    virtual bool input_ready();
    virtual uint32_t readkey();
    unsigned int shift;
private:
    gsi_edge_interrupt _irq;
};

#endif
