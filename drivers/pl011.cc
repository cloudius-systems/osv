/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 */

#include "pl011.hh"

static volatile char *const uart = (char *)0x9000000; /* UARTDR */

namespace console {

void PL011_Console::flush() {
    return;
}

bool PL011_Console::input_ready() {
    return false;
}

char PL011_Console::readch() {
    return 0;
}

void PL011_Console::dev_start() {
    return;
}

void PL011_Console::write(const char *str, size_t len) {
    while (len > 0) {
        if ((*str == '\n'))
            *uart = '\r';

        *uart = *str++;
        len--;
    }
}

}
