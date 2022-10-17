/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/power.hh>
#include <osv/debug.hh>
#include <osv/export.h>
#include <smp.hh>
#include <processor.hh>
#include <arch.hh>

#include <sys/reboot.h>

// For the arch-specific implementation see arch/$(ARCH)/power.cc
//
// NOTE: Please do not print informational messages from inside halt() or
// poweroff(), as they may be called in situations where OSV's state
// is questionable (e.g., abort()) so a debug() call might call further
// problems.

OSV_LIBC_API
int reboot(int cmd)
{
    switch ((unsigned)cmd) {
    case RB_POWER_OFF:
        printf("Power down\n");
        osv::poweroff();
        break;
    case RB_HALT_SYSTEM:
        printf("System halted\n");
        osv::halt();
        break;
    case RB_AUTOBOOT:
        printf("Restarting system\n");
        osv::reboot();
        break;
    case RB_SW_SUSPEND:
    case RB_ENABLE_CAD:
    case RB_DISABLE_CAD:
    case RB_KEXEC:
    default:
        return EINVAL;
    }

    return 0;
}
