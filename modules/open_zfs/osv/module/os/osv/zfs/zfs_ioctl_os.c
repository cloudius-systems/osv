// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ZFS ioctl OS-specific operations for OSv.
 *
 * OSv is a unikernel -- there is no userspace/kernel boundary,
 * so ioctl operations are minimal. We provide the required
 * function signatures for the common ioctl framework.
 */

#include <sys/types.h>
#include <sys/zfs_context.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/spa_impl.h>

/*
 * VFS reference management for ioctl operations.
 * On OSv these are simplified since we don't have the
 * full FreeBSD VFS busy/unbusy mechanism.
 */
int
zfs_vfs_ref(zfsvfs_t **zfvp)
{
	if (*zfvp == NULL)
		return (SET_ERROR(ESRCH));

	/* On OSv, just check the zfsvfs is valid */
	if ((*zfvp)->z_unmounted) {
		*zfvp = NULL;
		return (SET_ERROR(ESRCH));
	}
	return (0);
}

boolean_t
zfs_vfs_held(zfsvfs_t *zfsvfs)
{
	return (zfsvfs->z_vfs != NULL);
}

void
zfs_vfs_rele(zfsvfs_t *zfsvfs)
{
	(void) zfsvfs;
}

/*
 * Mount cache update (called when dataset properties change).
 * No-op on OSv since we don't cache mount statistics separately.
 */
void
zfs_ioctl_update_mount_cache(const char *dsname)
{
	(void) dsname;
}

/*
 * Maximum nvlist source size.
 */
uint64_t
zfs_max_nvlist_src_size_os(void)
{
	if (zfs_max_nvlist_src_size != 0)
		return (zfs_max_nvlist_src_size);

	return (KMALLOC_MAX_SIZE / 4);
}

/*
 * OS-specific ioctl registration.
 * OSv does not need jail/unjail or nextboot ioctls.
 */
void
zfs_ioctl_init_os(void)
{
}
