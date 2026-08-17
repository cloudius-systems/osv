// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ZFS Automatic Pool Upgrade - Header
 */

#ifndef _ZFS_AUTO_UPGRADE_H
#define	_ZFS_AUTO_UPGRADE_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Hook called after pool import to check and auto-upgrade ZFS pools
 * @param poolname Name of the pool that was imported
 */
void zfs_post_import_hook(const char *poolname);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_AUTO_UPGRADE_H */
