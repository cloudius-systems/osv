/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "vga.hh"
#include <osv/mmu.hh>

volatile unsigned short * const VGAConsole::_buffer
= reinterpret_cast<volatile unsigned short *>(mmu::phys_mem + 0xb8000);

static void tsm_log_cb(void *data, const char *file, int line, const char *func,
    const char *subs, unsigned int sev, const char *format, va_list args)
{
    printf("%s:%d:%s subs:%s sev:%d ", file, line, func, subs, sev);
    vprintf(format, args);
    printf("\n");
}

static void tsm_write_cb(struct tsm_vte *vte, const char *u8, size_t len,
    void *data)
{
    VGAConsole *vga = reinterpret_cast<VGAConsole *>(data);
    vga->push_queue(u8, len);
}

static int tsm_draw_cb(struct tsm_screen *screen, uint32_t id,
    const uint32_t *ch, size_t len, unsigned int cwidth, unsigned int posx,
    unsigned int posy, const struct tsm_screen_attr *attr, tsm_age_t age,
    void *data)
{
    VGAConsole *vga = reinterpret_cast<VGAConsole *>(data);

    if (len)
        vga->draw(*ch, attr, posx, posy);
    else
        vga->draw(' ', attr, posx, posy);
    return 0;
}

static int tsm_cursor_cb(struct tsm_screen *screen, unsigned int posx,
    unsigned int posy, void *data)
{
    VGAConsole *vga = reinterpret_cast<VGAConsole *>(data);

    vga->move_cursor(posx, posy);
    return 0;
}

static int tsm_scroll_cb(struct tsm_screen *screen, int scroll_count, void *data)
{
    VGAConsole *vga = reinterpret_cast<VGAConsole *>(data);

    vga->update_offset(scroll_count);
    return 0;
}

VGAConsole::VGAConsole(sched::thread* poll_thread, const termios *tio)
    : _tio(tio)
    , _kbd(poll_thread)
    , _offset_dirty(false)
{
    tsm_screen_new(&_tsm_screen, tsm_log_cb, this);
    tsm_screen_resize(_tsm_screen, NCOLS, NROWS);
    tsm_screen_set_max_sb(_tsm_screen, 1024);
    tsm_vte_new(&_tsm_vte, _tsm_screen, tsm_write_cb, this, tsm_log_cb, this);

    /* Leave first 8 lines 0x0, to clear BIOS message. */
    for (unsigned i = NCOLS * 8; i < BUFFER_SIZE; i++)
        _history[i] = 0x700 | ' ';

    /* This driver does not clear framebuffer, since most of hypervisor clears on start up */
}

void VGAConsole::push_queue(const char *str, size_t len)
{
    for (size_t i = 0; i < len; i++)
        _read_queue.push(str[i]);
}

void VGAConsole::draw(const uint32_t c, const struct tsm_screen_attr *attr,
    unsigned int x, unsigned int y)
{
    uint32_t c2 = ((attr->bccode << 4) | (attr->fccode & 0xf) << 8) |
        (c & 0xff);
    unsigned idx = (y + _offset) * NCOLS + x;

    if (_history[idx] != c2) {
        _buffer[idx] = c2;
        _history[idx] = c2;
    }
 }

void VGAConsole::move_cursor(unsigned int x, unsigned int y)
{
    uint16_t cursor = (y + _offset) * NCOLS + x;

    processor::outw((VGA_CRTC_CURSOR_LO) | (cursor & 0xff) << 8, VGA_CRT_IC);
    processor::outw((VGA_CRTC_CURSOR_HI) | ((cursor >> 8) & 0xff) << 8, VGA_CRT_IC);
}

void VGAConsole::update_offset(int scroll_count)
{
    _offset += scroll_count;

    /* Don't have anymore buffer, need to rotate */
    if (_offset > OFFSET_LIMIT || _offset < 0)
        _offset = 0;
    _offset_dirty = true;
}

void VGAConsole::apply_offset()
{
    uint16_t start = _offset * NCOLS;
    processor::outw((VGA_CRTC_START_LO) | (start & 0xff) << 8, VGA_CRT_IC);
    processor::outw((VGA_CRTC_START_HI) | ((start >> 8) & 0xff) << 8, VGA_CRT_IC);
    _offset_dirty = false;
}

void VGAConsole::write(const char *str, size_t len)
{
    while (len > 0) {
        if ((*str == '\n') && (_tio->c_oflag & OPOST) && (_tio->c_oflag & ONLCR))
            tsm_vte_input(_tsm_vte, "\r", 1);
        tsm_vte_input(_tsm_vte, str++, 1);
        len--;
    }
    tsm_screen_draw(_tsm_screen, tsm_draw_cb, tsm_cursor_cb, tsm_scroll_cb, this);
    if (_offset_dirty)
        apply_offset();
}

bool VGAConsole::input_ready()
{
    return !_read_queue.empty() || _kbd.input_ready();
}

char VGAConsole::readch()
{
    uint32_t key;
    unsigned int mods = 0;
    char c;

    while(1) {
        if (!_read_queue.empty()) {
            c = _read_queue.front();
            _read_queue.pop();
            return c;
        }

        key = _kbd.readkey();
        if (!key) {
            if (_read_queue.empty())
                return 0;
            continue;
        }

        if (_kbd.shift & MOD_SHIFT)
            mods |= TSM_SHIFT_MASK;
        if (_kbd.shift & MOD_CTL)
            mods |= TSM_CONTROL_MASK;
        if (_kbd.shift & MOD_ALT)
            mods |= TSM_ALT_MASK;

        switch (key) {
        case KEY_Up:
            tsm_screen_sb_up(_tsm_screen, 1);
            tsm_screen_draw(_tsm_screen, tsm_draw_cb, tsm_cursor_cb, tsm_scroll_cb, this);
            break;
        case KEY_Down:
            tsm_screen_sb_down(_tsm_screen, 1);
            tsm_screen_draw(_tsm_screen, tsm_draw_cb, tsm_cursor_cb, tsm_scroll_cb, this);
            break;
        case KEY_Page_Up:
            tsm_screen_sb_page_up(_tsm_screen, 1);
            tsm_screen_draw(_tsm_screen, tsm_draw_cb, tsm_cursor_cb, tsm_scroll_cb, this);
            break;
        case KEY_Page_Down:
            tsm_screen_sb_page_down(_tsm_screen, 1);
            tsm_screen_draw(_tsm_screen, tsm_draw_cb, tsm_cursor_cb, tsm_scroll_cb, this);
            break;
        default:
            if (tsm_vte_handle_keyboard(_tsm_vte, key, mods))
                tsm_screen_sb_reset(_tsm_screen);
        }
        if (_read_queue.empty())
            return 0;
    }
}
