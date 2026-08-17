// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * Sysctl stubs for OSv.
 * OSv does not have a sysctl interface. All ZFS tunables use
 * their compile-time defaults or are set programmatically.
 */

#include <sys/zfs_context.h>

/*
 * ZFS debug level (referenced by zfs_context_os.h ZFS_LOG macro).
 */
int zfs_debug_level = 0;
