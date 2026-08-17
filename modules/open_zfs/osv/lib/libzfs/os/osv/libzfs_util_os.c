// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv libzfs_util_os.c
 *
 * OSv-specific utility functions for libzfs.
 * No sysctl, no zones, no module loading, no /proc.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mnttab.h>
#include <sys/types.h>

#include <libzfs.h>
#include <libzfs_core.h>
#include <libzfs_impl.h>
#include "zfs_prop.h"
#include <libzutil.h>

/*
 * libzfs_error_init: return a human-readable error string for initialization
 * errors.
 */
const char *
libzfs_error_init(int error)
{
	switch (error) {
	case ENXIO:
		return ("ZFS kernel module not available.");
	case ENOENT:
		return ("/dev/zfs not found.");
	case EACCES:
		return ("Permission denied opening /dev/zfs.");
	default:
		return ("Failed to initialize the libzfs library.");
	}
}

/*
 * find_shares_object: OSv has no NFS shares directory.
 */
int
find_shares_object(differ_info_t *di)
{
	(void) di;
	return (0);
}

/*
 * zfs_destroy_snaps_nvl_os: no OS-specific cleanup needed on OSv.
 */
int
zfs_destroy_snaps_nvl_os(libzfs_handle_t *hdl, nvlist_t *snaps)
{
	(void) hdl, (void) snaps;
	return (0);
}

/*
 * zfs_version_kernel: return the kernel ZFS version string.
 * On OSv, libsolaris.so IS the kernel ZFS - return the compile-time version.
 */
char *
zfs_version_kernel(void)
{
	return (strdup(ZFS_META_ALIAS));
}

/*
 * zfs_userns: user namespaces not applicable on OSv.
 */
int
zfs_userns(zfs_handle_t *zhp, const char *nspath, int attach)
{
	(void) zhp, (void) nspath, (void) attach;
	errno = ENOTSUP;
	return (-1);
}

