/*
 * The xv6 software is:
 *
 * Copyright (c) 2006-2009 Frans Kaashoek, Robert Morris, Russ Cox,
 *                         Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "kbd.hh"

#define KBSTATP         0x64    // kbd controller status port(I)
#define KBS_DIB         0x01    // kbd data in buffer
#define KBDATAP         0x60    // kbd data port(I)

#define NO              0

// C('A') == Control-A
#define C(x) (uint32_t)(x - '@')

static uint32_t shiftcode[256] = {0,};

static uint32_t togglecode[256] = {0,};

static uint32_t normalmap[256] =
{
  NO,   KEY_Escape, '1',  '2',  '3',  '4',  '5',  '6',  // 0x00
  '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
  'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  // 0x10
  'o',  'p',  '[',  ']',  '\n', NO,   'a',  's',
  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 0x20
  '\'', '`',  NO,   '\\', 'z',  'x',  'c',  'v',
  'b',  'n',  'm',  ',',  '.',  '/',  NO,   '*',  // 0x30
  NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
  NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',  // 0x40
  '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
  '2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,   // 0x50
};

static uint32_t shiftmap[256] =
{
  NO,   KEY_Escape,  '!',  '@',  '#',  '$',  '%',  '^',  // 0x00
  '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
  'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  // 0x10
  'O',  'P',  '{',  '}',  '\n', NO,   'A',  'S',
  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  // 0x20
  '"',  '~',  NO,   '|',  'Z',  'X',  'C',  'V',
  'B',  'N',  'M',  '<',  '>',  '?',  NO,   '*',  // 0x30
  NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
  NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',  // 0x40
  '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
  '2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,   // 0x50
};

static uint32_t ctlmap[256] =
{
  NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO,
  NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO,
  C('Q'),  C('W'),  C('E'),  C('R'),  C('T'),  C('Y'),  C('U'),  C('I'),
  C('O'),  C('P'),  NO,      NO,      '\r',    NO,      C('A'),  C('S'),
  C('D'),  C('F'),  C('G'),  C('H'),  C('J'),  C('K'),  C('L'),  NO,
  NO,      NO,      NO,      C('\\'), C('Z'),  C('X'),  C('C'),  C('V'),
  C('B'),  C('N'),  C('M'),  NO,      NO,      C('/'),  NO,      NO,
};

Keyboard::Keyboard(sched::thread* poll_thread)
    : _irq(1, [=] { poll_thread->wake(); })
{
  shiftcode[0x1D] = MOD_CTL;
  shiftcode[0x2A] = MOD_SHIFT;
  shiftcode[0x36] = MOD_SHIFT;
  shiftcode[0x38] = MOD_ALT;
  shiftcode[0x9D] = MOD_CTL;
  shiftcode[0xB8] = MOD_ALT;

  togglecode[0x3A] = MOD_CAPSLOCK;
  togglecode[0x45] = MOD_NUMLOCK;
  togglecode[0x46] = MOD_SCROLLLOCK;

  normalmap[0x9C] = '\n';      // KP_Enter
  normalmap[0xB5] = '/';       // KP_Div
  normalmap[0xC8] = KEY_Up;    normalmap[0xD0] = KEY_Down;
  normalmap[0xC9] = KEY_Page_Up;  normalmap[0xD1] = KEY_Page_Down;
  normalmap[0xCB] = KEY_Left;    normalmap[0xCD] = KEY_Right;
  normalmap[0x97] = KEY_Home;  normalmap[0xCF] = KEY_End;
  normalmap[0xD2] = KEY_Insert;   normalmap[0xD3] = KEY_Delete;

  shiftmap[0x9C] = '\n';      // KP_Enter
  shiftmap[0xB5] = '/';       // KP_Div
  shiftmap[0xC8] = KEY_Up;    shiftmap[0xD0] = KEY_Down;
  shiftmap[0xC9] = KEY_Page_Up;  shiftmap[0xD1] = KEY_Page_Down;
  shiftmap[0xCB] = KEY_Left;    shiftmap[0xCD] = KEY_Right;
  shiftmap[0x97] = KEY_Home;  shiftmap[0xCF] = KEY_End;
  shiftmap[0xD2] = KEY_Insert;   shiftmap[0xD3] = KEY_Delete;

  ctlmap[0x9C] = '\n';      // KP_Enter
  ctlmap[0xB5] = C('/');    // KP_Div
  ctlmap[0xC8] = KEY_Up;    ctlmap[0xD0] = KEY_Down;
  ctlmap[0xC9] = KEY_Page_Up;  ctlmap[0xD1] = KEY_Page_Down;
  ctlmap[0xCB] = KEY_Left;    ctlmap[0xCD] = KEY_Right;
  ctlmap[0x97] = KEY_Home;  ctlmap[0xCF] = KEY_End;
  ctlmap[0xD2] = KEY_Insert;   ctlmap[0xD3] = KEY_Delete;
}

bool Keyboard::input_ready()
{
    unsigned int st = processor::inb(KBSTATP);
    return ((st & KBS_DIB) != 0);
}

uint32_t Keyboard::readkey()
{
    static uint32_t *charcode[4] = {
        normalmap, shiftmap, ctlmap, ctlmap
    };
    unsigned char st, data;
    uint32_t c;

    st = processor::inb(KBSTATP);
    if ((st & KBS_DIB) == 0)
        return 0;
    data = processor::inb(KBDATAP);

    if (data == 0xE0) {
        shift |= MOD_E0ESC;
        return 0;
    } else if (data & 0x80) {
        // Key released
        data = (shift & MOD_E0ESC ? data : data & 0x7F);
        shift &= ~(shiftcode[data] | MOD_E0ESC);
        return 0;
    } else if (shift & MOD_E0ESC) {
        // Last character was an E0 escape; or with 0x80
        data |= 0x80;
        shift &= ~MOD_E0ESC;
    }

    shift |= shiftcode[data];
    shift ^= togglecode[data];

    c = charcode[shift & (MOD_CTL | MOD_SHIFT)][data];
    if (shift & MOD_CAPSLOCK) {
        if('a' <= c && c <= 'z')
            c += 'A' - 'a';
        else if('A' <= c && c <= 'Z')
            c += 'a' - 'A';
    }
    return c;
}
