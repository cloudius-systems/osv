/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_LINE_DISCIPLINE_HH
#define DRIVERS_LINE_DISCIPLINE_HH
#include <termios.h>
#include <queue>
#include <list>
#include <functional>
#include <osv/uio.h>
#include <osv/mutex.h>
#include "console-driver.hh"

namespace console {

class LineDiscipline {
public:
    explicit LineDiscipline(const termios *tio);
    void read(struct uio *uio, int ioflag);
    void read_poll(console_driver *driver);
    int read_queue_size() { return _read_queue.size(); }
    void write(const char *str, size_t len,
        std::function<void(const char *str, size_t len)> writer);
private:
    mutex _mutex;
    const termios *_tio;
    // characters available to be returned on read() from the console
    std::queue<char> _read_queue;
    // who to wake when characters are added to _read_queue
    std::list<sched::thread*> _readers;
    // inputed characters not yet made available to read() in ICANON mode
    std::deque<char> _line_buffer;
};

};

#endif
