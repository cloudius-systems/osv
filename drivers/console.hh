/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_CONSOLE_HH
#define DRIVERS_CONSOLE_HH

#include <boost/format.hpp>

namespace console {

class Console {
public:
    virtual ~Console() {}
    virtual void write(const char *str, size_t len) = 0;
    virtual bool input_ready() = 0;
    virtual char readch() = 0;
};

void write(const char *msg, size_t len);
void write_ll(const char *msg, size_t len);
void console_init(bool use_vga);
int open(void);

}

#endif
