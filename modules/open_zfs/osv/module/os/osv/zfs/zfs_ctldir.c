// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ZFS control directory (.zfs) stubs for OSv.
 * The .zfs control directory provides snapshot access.
 * Not implemented on OSv initially.
 */

#include <sys/zfs_context.h>
#include <sys/zfs_ctldir.h>

int
zfsctl_snapshot_unmount(const char *snapname, int flags)
{
	(void) snapname; (void) flags;
	return (0);
}
