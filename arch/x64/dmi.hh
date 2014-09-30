/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef X64_DMI_HH
#define X64_DMI_HH

#include <string>

extern std::string dmi_bios_vendor;

void dmi_probe();

#endif
