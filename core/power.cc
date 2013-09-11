#include <osv/power.hh>
#include <debug.hh>
#include <smp.hh>
#include <processor.hh>

extern "C" {
#include "acpi.h"
}

namespace osv {

// NOTE: Please do not print informational messages from inside halt() or
// poweroff(), as they may be called in situations where OSV's state
// is questionable (e.g., abort()) so a debug() call might call further
// problems.

void halt(void)
{
    crash_other_processors();
    while (true)
        processor::halt_no_interrupts();
}

void poweroff(void)
{
    ACPI_STATUS status;
    status = AcpiInitializeSubsystem();
    if (ACPI_FAILURE(status)) {
        debug("AcpiInitializeSubsystem failed: %s\n", AcpiFormatException(status));
        halt();
    }
    status = AcpiLoadTables();
    if (ACPI_FAILURE(status)) {
        debug("AcpiLoadTables failed: %s\n", AcpiFormatException(status));
        halt();
    }

    status = AcpiEnterSleepStatePrep(ACPI_STATE_S5);
    if (ACPI_FAILURE(status)) {
        debug("AcpiEnterSleepStatePrep failed: %s\n", AcpiFormatException(status));
        halt();
    }
    status = AcpiEnterSleepState(ACPI_STATE_S5);
    if (ACPI_FAILURE(status)) {
        debug("AcpiEnterSleepState failed: %s\n", AcpiFormatException(status));
        halt();
    }
    // We shouldn't get here.
    halt();
}

// reboot() does not normally return, but may return if the reboot magic for
// some reson fails.
void reboot(void)
{
    // It would be nice if AcpiReset() worked, but it doesn't seem to work
    // (on qemu & kvm), so let's resort to brute force...
    processor::outb(1, 0x92);
    debug("osv::reboot() did not work :(\n");
}


} /* namespace osv */
