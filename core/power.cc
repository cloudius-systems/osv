#include <osv/power.hh>
#include <debug.hh>
#include <smp.hh>

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

} /* namespace osv */
