/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef BSD_MACHINE_CPU_H
#define BSD_MACHINE_CPU_H

#include <sys/types.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

/*
 * Return contents of in-cpu fast counter as a sort of "bogo-time"
 * for random-harvesting purposes.
 */
uint64_t get_cyclecount();

__END_DECLS

#endif
