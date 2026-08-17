// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ZFS resource accounting stubs for OSv.
 * OSv does not have RACCT, so just track I/O stats.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>

void
zfs_racct_read(spa_t *spa, uint64_t size, uint64_t iops, uint32_t flags)
{
	spa_iostats_read_add(spa, size, iops, flags);
}

void
zfs_racct_write(spa_t *spa, uint64_t size, uint64_t iops, uint32_t flags)
{
	spa_iostats_write_add(spa, size, iops, flags);
}
