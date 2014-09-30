/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_FIRMWARE_HH
#define OSV_FIRMWARE_HH

#include <string>

namespace osv {

void firmware_probe();

std::string firmware_vendor();

}

#endif
