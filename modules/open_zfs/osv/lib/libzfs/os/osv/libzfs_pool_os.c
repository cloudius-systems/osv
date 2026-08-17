// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv libzfs_pool_os.c
 *
 * OSv pool devices are /dev/vblk* (VirtIO block devices).  There is no
 * udev, no EFI partition relabeling, no disk-by-id symlinks.
 * All disk-label and OS-specific pool functions are stubs.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include <libzutil.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include <libzfs_impl.h>
#include "zfs_comutil.h"
#include "zfeature_common.h"

/*
 * zpool_relabel_disk: OSv VirtIO block devices don't need EFI relabeling.
 */
int
zpool_relabel_disk(libzfs_handle_t *hdl, const char *path, const char *msg)
{
	(void) hdl, (void) path, (void) msg;
	return (0);
}

/*
 * zpool_label_disk: OSv VirtIO block devices don't need GPT/EFI labeling.
 * The whole disk is used directly as a ZFS vdev.
 */
int
zpool_label_disk(libzfs_handle_t *hdl, zpool_handle_t *zhp, const char *name)
{
	(void) hdl, (void) zhp, (void) name;
	return (0);
}

/*
 * zpool_disk_wait: no udev on OSv, device is available immediately.
 */
int
zpool_disk_wait(const char *path)
{
	(void) path;
	return (0);
}

/*
 * zpool_label_disk_wait: no udev settle time needed.
 */
int
zpool_label_disk_wait(const char *path, int timeout_ms)
{
	(void) path, (void) timeout_ms;
	return (0);
}
