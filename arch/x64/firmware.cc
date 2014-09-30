/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/firmware.hh>

#include "dmi.hh"

namespace osv {

void firmware_probe()
{
    dmi_probe();
}

std::string firmware_vendor()
{
    return dmi_bios_vendor;
}

}
