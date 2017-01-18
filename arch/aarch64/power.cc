/*
 * Copyright (C) 2015 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <smp.hh>
#include <osv/power.hh>
#include <osv/debug.hh>
#include <osv/interrupt.hh>
#include <arch.hh>

#include "psci.hh"
#include <string.h>

namespace osv {

void halt(void)
{
    smp_crash_other_processors();

    while (true) {
        arch::halt_no_interrupts();
    }
}

void poweroff(void)
{
    int ret = psci::_psci.system_off();
    debug_early("power: poweroff failed: ");
    debug_early(strerror(ret));
    debug_early("\n");
    halt();
}

void reboot(void)
{
    int ret = psci::_psci.system_reset();
    debug_early("power: reboot failed: ");
    debug_early(strerror(ret));
    debug_early("\n");
}

} /* namespace osv */
