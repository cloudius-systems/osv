/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <drivers/debug-console.hh>

static volatile char *const uart = (char *)0x9000000; /* UART0DR */

static void simple_write(const char *str, size_t len)
{
    while (len > 0) {
        if ((*str == '\n'))
            *uart = '\r';
        *uart = *str++;
        len--;
    }
}
void debug_console::set_impl(Console* impl)
{
    WITH_LOCK(_lock) {
        _impl = impl;
    }
}

void debug_console::write(const char* str, size_t len)
{
    WITH_LOCK(_lock) {
        if (_impl) {
            _impl->write(str, len);
        } else {
            simple_write(str, len);
        }
    }
}

void debug_console::write_ll(const char *str, size_t len)
{
    if (_impl) {
        _impl->write(str, len);
    } else
        simple_write(str, len);
}

char debug_console::readch()
{
    WITH_LOCK(_lock) {
        return _impl ? _impl->readch() : 0;
    }
 }

bool debug_console::input_ready()
{
    WITH_LOCK(_lock) {
        return _impl && _impl->input_ready();
    }
}
