/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DRIVERS_CONSOLE_DRIVER_HH
#define DRIVERS_CONSOLE_DRIVER_HH

#include <functional>
#include <osv/sched.hh>

namespace console {

class console_driver {
public:
    virtual ~console_driver() {}
    virtual void write(const char *str, size_t len) = 0;
    virtual void flush() = 0;
    virtual bool input_ready() = 0;
    virtual char readch() = 0;
    void start(std::function<void()> read_poll);
protected:
    sched::thread *_thread;
private:
    virtual void dev_start() = 0;
    virtual const char *thread_name() = 0;
};

};

#endif
