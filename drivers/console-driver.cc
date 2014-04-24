/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "console-driver.hh"

namespace console {

void ConsoleDriver::start(std::function<void()> read_poll)
{
        _thread = new sched::thread([&] { read_poll(); },
            sched::thread::attr().name(thread_name()));
        dev_start();
        _thread->start();
}

};
