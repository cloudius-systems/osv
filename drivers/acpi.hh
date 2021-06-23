/*
 * Copyright (C) 2013 Nodalink, SARL.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#ifndef _OSV_DRIVER_ACPI_HH_
#define _OSV_DRIVER_ACPI_HH_

#include "acpi.h"

namespace acpi {

extern uint64_t pvh_rsdp_paddr;

void init();
bool is_enabled();

}

#endif //!_OSV_DRIVER_ACPI_HH_
