/*
 * Copyright (C) 2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef XEN_CONSOLE_HH
#define XEN_CONSOLE_HH

#include "console-driver.hh"
#include <xen/interface/xen.h>
#include <xen/interface/grant_table.h>

namespace console {

class XEN_Console : public console_driver {
public:
    XEN_Console();
    virtual void write(const char *str, size_t len);
    virtual void flush();
    virtual bool input_ready();
    virtual char readch();

private:
    virtual void dev_start();
    virtual const char *thread_name() { return "xen-input"; }
    void handle_intr();
    static void console_intr(void *arg) {
        static_cast<XEN_Console*>(arg)->handle_intr();
    }

    struct xencons_interface *_interface;
    int _evtchn;
    unsigned int _irq;
    unsigned long _pfn;
};

}

#endif /* XEN_CONSOLE_HH */
