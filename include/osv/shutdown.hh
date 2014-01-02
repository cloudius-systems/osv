/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef INCLUDED_OSV_SHUTDOWN_H
#define INCLUDED_OSV_SHUTDOWN_H

namespace osv {

/**
* Powers off the machine with best effort to gracefully release resources.
* Unmounts file systems.
*/
void shutdown() __attribute__((noreturn));

}

#endif
