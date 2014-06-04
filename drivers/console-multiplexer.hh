/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_CONSOLE_MULTIPLEXER_HH
#define DRIVERS_CONSOLE_MULTIPLEXER_HH
#include <osv/spinlock.h>
#include <termios.h>
#include "console-driver.hh"
#include "line-discipline.hh"

namespace console {

class console_multiplexer {
public:
    explicit console_multiplexer(const termios *tio, console_driver *early_driver = nullptr);
    ~console_multiplexer() {};
    void driver_add(console_driver *driver);
    void start();
    void read(struct uio *uio, int ioflag);
    void write_ll(const char *str, size_t len);
    void write(const char *str, size_t len);
    void write(struct uio *uio, int ioflag);
    int read_queue_size();
private:
    void drivers_write(const char *str, size_t len);
    void drivers_flush();
    const termios *_tio;
    spinlock _early_lock;
    bool _started = false;
    console_driver *_early_driver;
    std::list<console_driver *> _drivers;
    mutex _mutex;
    LineDiscipline *_ldisc;
};

};

#endif
