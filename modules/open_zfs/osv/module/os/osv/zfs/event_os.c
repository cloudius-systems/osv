// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * Event notification stubs for OSv.
 * OSv does not use kqueue/kevent, so these are no-ops.
 */

#include <sys/types.h>

/*
 * No event notification support needed on OSv.
 * The FreeBSD version implements knlist_init_sx for kqueue integration,
 * which OSv doesn't have.
 */
