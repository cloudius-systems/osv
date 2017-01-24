/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef EARLY_CONSOLE_HH
#define EARLY_CONSOLE_HH

#include <drivers/console-driver.hh>
#include <drivers/pl011.hh>
#include <drivers/xenconsole.hh>

namespace console {

union AARCH64_Console {
    PL011_Console pl011;
    XEN_Console xen;

    AARCH64_Console() {};  /* placement new is used to initialize object */
    ~AARCH64_Console() {}; /* won't ever be called */
};

extern AARCH64_Console aarch64_console;
extern console_driver & arch_early_console;

}

#endif /* EARLY_CONSOLE_HH */
