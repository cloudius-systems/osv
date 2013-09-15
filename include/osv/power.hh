/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INCLUDED_OSV_POWER_H
#define INCLUDED_OSV_POWER_H

namespace osv {

void halt() __attribute__((noreturn));
void poweroff() __attribute__((noreturn));
void reboot();

}

#endif /* INCLUDED_OSV_POWER_H */
