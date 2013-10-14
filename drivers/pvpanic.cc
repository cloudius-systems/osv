/*
 * Copyright (C) 2013 Nodalink, SARL.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

extern "C" {
    #include "acpi.h"
}

#include "msr.hh"
#include "debug.hh"
#include "drivers/acpi.hh"
#include "drivers/pvpanic.hh"

namespace panic {
namespace pvpanic {

static u32 port;

void probe_and_setup()
{
    ACPI_BUFFER results;
    ACPI_OBJECT obj;
    ACPI_STATUS status;

    results.Length = sizeof(obj);
    results.Pointer = &obj;
    status = AcpiEvaluateObject(nullptr,
                                (ACPI_STRING) "\\_SB.PCI0.ISA.PEVT.PEST",
                                nullptr,
                                &results);
    if (status == AE_NOT_FOUND) {
        return;
    }

    if (ACPI_FAILURE(status)) {
        debug("pvpanic:AcpiEvaluateObject() failed: %s\n",
              AcpiFormatException(status));
        return;
    }

    if (obj.Integer.Type != ACPI_TYPE_INTEGER) {
        return;
    }

    // No need to free memory: See 8.10 page 206 of ACPICA documentation

    port = obj.Integer.Value;
}

void panicked()
{
    if (port) {
        processor::outb(1, port);
    }
}

}}
