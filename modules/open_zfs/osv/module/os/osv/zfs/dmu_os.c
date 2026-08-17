// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * DMU OS-specific operations for OSv.
 *
 * The FreeBSD version implements dmu_read_pages/dmu_write_pages for
 * VM page integration. OSv does not have a FreeBSD-style VM page cache,
 * so these are not needed. All I/O goes through the ARC and ABD layer.
 */

#include <sys/types.h>
#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/zfs_context.h>

/*
 * OSv does not need page-level DMU operations.
 * All data transfer uses ABD (Adaptive Buffer Descriptors) which
 * work with flat buffers on OSv.
 */
