// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv zpool_vdev_os.c
 *
 * OSv-specific vdev management for zpool.  On OSv:
 *  - No SCSI/SG ioctls (no /dev/sg*)
 *  - No blkid library
 *  - No EFI partition library
 *  - No udev/sysfs power management
 *  - VirtIO block devices /dev/vblkN do not need sector-size probing
 *
 * All Linux-specific functions are stubbed out.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mntent.h>

#include <libzutil.h>
#include <libzfs.h>
#include "zpool_util.h"

/*
 * check_sector_size_database: On OSv, VirtIO block devices always use
 * the default 512-byte sector size.  No SCSI inquiry needed.
 */
boolean_t
check_sector_size_database(char *path, int *sector_size)
{
	(void) path;
	(void) sector_size;
	return (B_FALSE);
}

/*
 * check_device: verify a device is safe to use as a vdev.
 * On OSv, we just check it can be opened.
 */
int
check_device(const char *path, boolean_t force,
    boolean_t isspare, boolean_t iswholedisk)
{
	(void) force, (void) isspare, (void) iswholedisk;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		(void) fprintf(stderr, "cannot open '%s': %s\n",
		    path, strerror(errno));
		return (-1);
	}
	(void) close(fd);
	return (0);
}

/*
 * check_file: verify a file-based vdev is safe to use.
 */
int
check_file(const char *file, boolean_t force, boolean_t isspare)
{
	return (check_file_generic(file, force, isspare));
}

/*
 * after_zpool_upgrade: called after a pool upgrade.  No-op on OSv.
 */
void
after_zpool_upgrade(zpool_handle_t *zhp)
{
	(void) zhp;
}

/*
 * zpool_power_current_state: no enclosure power management on OSv.
 */
int
zpool_power_current_state(zpool_handle_t *zhp, char *vdev)
{
	(void) zhp, (void) vdev;
	return (-1); /* unsupported */
}

/*
 * zpool_power: no enclosure power management on OSv.
 */
int
zpool_power(zpool_handle_t *zhp, char *vdev, boolean_t turn_on)
{
	(void) zhp, (void) vdev, (void) turn_on;
	return (ENOTSUP);
}
