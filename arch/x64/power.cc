/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/power.hh>
#include <osv/debug.hh>
#include <smp.hh>
#include <processor.hh>
#include <arch.hh>

extern "C" {
#include "acpi.h"
}

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
    ACPI_STATUS status = AcpiEnterSleepStatePrep(ACPI_STATE_S5);
    if (ACPI_FAILURE(status)) {
        debug("AcpiEnterSleepStatePrep failed: %s\n", AcpiFormatException(status));
        halt();
    }
    status = AcpiEnterSleepState(ACPI_STATE_S5);
    if (ACPI_FAILURE(status)) {
        debug("AcpiEnterSleepState failed: %s\n", AcpiFormatException(status));
        halt();
    }

    // We shouldn't get here on x86.
    halt();
}

void reboot(void)
{
    // It would be nice if AcpiReset() worked, but it doesn't seem to work
    // (on qemu & kvm), so let's resort to other techniques, which appear
    // to work. Hopefully one of them will work on any hypervisor.
    // Method 1: "fast reset" via System Control Port A (port 0x92)
    processor::outb(1, 0x92);
    // Method 2: Reset using the 8042 PS/2 Controller ("keyboard controller")
    processor::outb(0xfe, 0x64);
    // Method 3: Cause triple fault by loading a broken IDT and triggering an
    // interrupt.
    processor::lidt(processor::desc_ptr(0, 0));
    __asm__ __volatile__("int3");
    // If we're still here, none of the above methods worked...
}

} /* namespace osv */
