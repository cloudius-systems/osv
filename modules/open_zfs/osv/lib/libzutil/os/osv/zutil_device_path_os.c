// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv zutil_device_path_os.c
 *
 * Device path utilities for OSv.  VirtIO block devices appear as
 * /dev/vblk0, /dev/vblk0.1, /dev/vblk1, etc.  No udev, no /sys,
 * no /dev/disk/by-id.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libzutil.h>

/*
 * OSv device search path: only /dev.
 */
static const char * const zpool_default_import_path[] = {
	"/dev"
};

const char * const *
zpool_default_search_paths(size_t *count)
{
	*count = 1;
	return (zpool_default_import_path);
}

/*
 * zfs_append_partition: OSv VirtIO block devices use the naming
 * convention /dev/vblk0.1 for partition 1 of disk 0.  Crucible
 * volumes (/dev/crucibleN) are presented as raw, unpartitioned block
 * devices and must NOT have a ".1" suffix appended.
 */
int
zfs_append_partition(char *path, size_t max_len)
{
	int len = strlen(path);
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;

	/* Raw, unpartitioned devices: do not append ".1". */
	if (strncmp(base, "crucible", 8) == 0) {
		return (len);
	}

	/*
	 * OSv exposes a partitioned disk as /dev/vblkN.M children (created by
	 * read_partition_table() only when a valid MBR is present) and a raw,
	 * unpartitioned disk directly as /dev/vblkN with no child node.  Note
	 * OSv names MBR slots 0-based, so the first partition is /dev/vblkN.0,
	 * not vblkN.1.  Linux/FreeBSD GPT-label a whole disk and use partition
	 * 1; OSv does no such labeling -- ZFS writes its labels to the whole
	 * raw device.  So only append ".1" when that partition node actually
	 * exists; otherwise the base path is a whole raw disk and must be used
	 * as-is (e.g. 'zpool create test /dev/vblk1' on a wiped NVMe).
	 */
	if (len + 2 >= (int)max_len)
		return (-1);

	char candidate[MAXPATHLEN];
	(void) snprintf(candidate, sizeof (candidate), "%s.1", path);
	if (access(candidate, F_OK) != 0) {
		/* No ".1" partition node: whole raw disk, leave path as-is. */
		return (len);
	}

	strcat(path, ".1");
	return (len + 2);
}

/*
 * zfs_strip_partition: remove partition suffix from a vdev path.
 * /dev/vblk0.1 -> /dev/vblk0
 *
 * Caller must free the returned string.
 */
char *
zfs_strip_partition(const char *path)
{
	char *tmp = strdup(path);
	char *dot;

	if (!tmp)
		return (NULL);

	/* Strip trailing .N (partition suffix) */
	dot = strrchr(tmp, '.');
	if (dot != NULL && dot != tmp) {
		/* Only strip if suffix is all digits */
		char *p = dot + 1;
		int all_digits = (*p != '\0');
		while (*p) {
			if (!isdigit((unsigned char)*p)) {
				all_digits = 0;
				break;
			}
			p++;
		}
		if (all_digits)
			*dot = '\0';
	}

	return (tmp);
}

/*
 * zfs_strip_path: strip the /dev/ prefix, returning just the device name.
 */
const char *
zfs_strip_path(const char *path)
{
	size_t count;
	const char * const *spaths = zpool_default_search_paths(&count);

	for (size_t i = 0; i < count; i++) {
		size_t plen = strlen(spaths[i]);
		if (strncmp(path, spaths[i], plen) == 0 &&
		    path[plen] == '/')
			return (path + plen + 1);
	}
	return (path);
}

/*
 * zfs_get_underlying_path: return underlying device for a given path.
 * On OSv there are no symlinks or DM devices, just return the path as-is
 * (stripped of any partition suffix).
 *
 * Caller must free returned string.
 */
char *
zfs_get_underlying_path(const char *dev_name)
{
	char *tmp;

	if (dev_name == NULL)
		return (NULL);

	tmp = realpath(dev_name, NULL);
	if (tmp == NULL)
		tmp = strdup(dev_name);

	/* Strip partition suffix */
	char *result;
	const char *base = strrchr(tmp, '/');
	if (base != NULL) {
		char *stripped = zfs_strip_partition(base + 1);
		if (stripped) {
			size_t dirlen = (base - tmp) + 1;
			result = malloc(dirlen + strlen(stripped) + 1);
			if (result) {
				strncpy(result, tmp, dirlen);
				result[dirlen] = '\0';
				strcat(result, stripped);
			}
			free(stripped);
		} else {
			result = tmp;
			tmp = NULL;
		}
	} else {
		result = zfs_strip_partition(tmp);
	}

	free(tmp);
	return (result);
}

/*
 * zfs_dev_is_whole_disk: on OSv, VirtIO block devices follow the naming
 * convention /dev/vblkN (whole disk) vs /dev/vblkN.P (partition P).
 * A path with a '.' in the basename is already a partition, not a whole disk.
 * This prevents zfs_append_partition() from appending a second ".1" suffix
 * when zpool create/import is given a partition path like /dev/vblk0.1.
 *
 * Crucible volumes (/dev/crucibleN) are raw, unpartitioned network block
 * devices.  Treat them as already-partition (B_FALSE) so the libzfs caller
 * does not try to append ".1".
 */
boolean_t
zfs_dev_is_whole_disk(const char *dev_name)
{
	const char *last_slash = strrchr(dev_name, '/');
	const char *base = (last_slash != NULL) ? last_slash + 1 : dev_name;

	/* Crucible: never partitioned, present as raw block device. */
	if (strncmp(base, "crucible", 8) == 0)
		return (B_FALSE);

	/* If the basename contains '.', it is already a partition. */
	return (strchr(base, '.') == NULL ? B_TRUE : B_FALSE);
}

/*
 * zfs_dev_is_dm: no device mapper on OSv.
 */
boolean_t
zfs_dev_is_dm(const char *dev_name)
{
	(void) dev_name;
	return (B_FALSE);
}

/*
 * is_mpath_whole_disk: no multipath on OSv.
 */
boolean_t
is_mpath_whole_disk(const char *path)
{
	(void) path;
	return (B_FALSE);
}

/*
 * zfs_get_enclosure_sysfs_path: no enclosure management on OSv.
 */
char *
zfs_get_enclosure_sysfs_path(const char *dev_name)
{
	(void) dev_name;
	return (NULL);
}

/*
 * update_vdev_config_dev_sysfs_path: no sysfs on OSv.
 */
void
update_vdev_config_dev_sysfs_path(nvlist_t *nv, const char *path,
    const char *key)
{
	(void) nv, (void) path, (void) key;
}

/*
 * update_vdevs_config_dev_sysfs_path: no sysfs on OSv.
 */
void
update_vdevs_config_dev_sysfs_path(nvlist_t *config)
{
	(void) config;
}

/*
 * update_vdev_config_dev_strs: no persistent device IDs on OSv.
 * Clear any stale devid/phys_path entries.
 */
void
update_vdev_config_dev_strs(nvlist_t *nv)
{
	(void) nvlist_remove_all(nv, ZPOOL_CONFIG_DEVID);
	(void) nvlist_remove_all(nv, ZPOOL_CONFIG_PHYS_PATH);
	(void) nvlist_remove_all(nv, ZPOOL_CONFIG_VDEV_ENC_SYSFS_PATH);
}
