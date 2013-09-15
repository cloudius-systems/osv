/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DEBUG_CONSOLE_HH_
#define DEBUG_CONSOLE_HH_

#include "console.hh"
#include <osv/mutex.h>

// Wrap a Console with a spinlock, used for debugging
// (we can't use a mutex, since we might want to debug the scheduler)

class debug_console : public Console {
public:
    void set_impl(Console* impl);
    virtual void write(const char *str, size_t len);
    // write without taking any locks
    void write_ll(const char *str, size_t len);
    virtual bool input_ready() override;
    virtual char readch();
private:
    Console* _impl;
    spinlock _lock;
};


#endif /* DEBUG_CONSOLE_HH_ */
