/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/prio.hh>
#include <osv/xen.hh>

#include "early-console.hh"

namespace console {

union AARCH64_Console aarch64_console;
console_driver & arch_early_console = aarch64_console.pl011;

}
