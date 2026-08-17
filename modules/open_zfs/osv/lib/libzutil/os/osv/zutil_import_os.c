// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv zutil_import_os.c
 *
 * Pool import support for OSv.  Scans /dev/vblk* devices for ZFS pool labels.
 * No udev, no blkid, no /sys - simple directory scan of /dev.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libzutil.h>
#include <libnvpair.h>
#include <sys/fs/zfs.h>
#include <sys/vdev_impl.h>

#include "zutil_import.h"

/*
 * zfs_dev_flush: flush device write cache.
 * On OSv VirtIO block devices, there's no kernel buffer cache to flush
 * in the same way - just return 0.
 */
int
zfs_dev_flush(int fd)
{
	(void) fd;
	return (0);
}

/*
 * zpool_open_func: read ZFS label from a device node and add it to the
 * import cache.  Called by the import thread pool.
 */
void
zpool_open_func(void *arg)
{
	rdsk_node_t *rn = arg;
	libpc_handle_t *hdl = rn->rn_hdl;
	struct stat statbuf;
	nvlist_t *config;
	uint64_t vdev_guid = 0;
	int error;
	int num_labels = 0;
	int fd;

	/*
	 * Ignore failed stats.  We only want block devices (or regular files
	 * for testing).
	 */
	if (stat(rn->rn_name, &statbuf) != 0 ||
	    (!S_ISREG(statbuf.st_mode) && !S_ISBLK(statbuf.st_mode)) ||
	    (S_ISREG(statbuf.st_mode) &&
	    (uint64_t)statbuf.st_size < SPA_MINDEVSIZE))
		return;

	fd = open(rn->rn_name, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		if (errno == EACCES)
			hdl->lpc_open_access_error = B_TRUE;
		return;
	}

	error = zpool_read_label(fd, &config, &num_labels);
	if (error != 0) {
		(void) close(fd);
		return;
	}

	if (num_labels == 0) {
		(void) close(fd);
		nvlist_free(config);
		return;
	}

	/*
	 * Check that the vdev is for the expected guid.
	 */
	error = nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &vdev_guid);
	if (error || (rn->rn_vdev_guid && rn->rn_vdev_guid != vdev_guid)) {
		(void) close(fd);
		nvlist_free(config);
		return;
	}

	(void) close(fd);

	rn->rn_config = config;
	rn->rn_num_labels = num_labels;
}

/*
 * zpool_find_import_blkid: enumerate /dev/vblk* devices and add them to
 * the import candidate list.  On OSv we cannot use blkid, so we do a
 * simple opendir("/dev") scan instead.
 */
int
zpool_find_import_blkid(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t **slice_cache)
{
	DIR *dp;
	struct dirent *ep;
	rdsk_node_t *slice;
	avl_index_t where;

	*slice_cache = NULL;

	dp = opendir("/dev");
	if (dp == NULL)
		return (errno);

	*slice_cache = zutil_alloc(hdl, sizeof (avl_tree_t));
	avl_create(*slice_cache, slice_cache_compare, sizeof (rdsk_node_t),
	    offsetof(rdsk_node_t, rn_node));

	while ((ep = readdir(dp)) != NULL) {
		char fullpath[MAXPATHLEN];

		/* Only consider vblk devices */
		if (strncmp(ep->d_name, "vblk", 4) != 0)
			continue;

		(void) snprintf(fullpath, sizeof (fullpath),
		    "/dev/%s", ep->d_name);

		slice = zutil_alloc(hdl, sizeof (rdsk_node_t));
		slice->rn_name = zutil_strdup(hdl, fullpath);
		slice->rn_vdev_guid = 0;
		slice->rn_lock = lock;
		slice->rn_avl = *slice_cache;
		slice->rn_hdl = hdl;
		slice->rn_order = IMPORT_ORDER_DEFAULT;
		slice->rn_labelpaths = B_FALSE;

		pthread_mutex_lock(lock);
		if (avl_find(*slice_cache, slice, &where)) {
			free(slice->rn_name);
			free(slice);
		} else {
			avl_insert(*slice_cache, slice, where);
		}
		pthread_mutex_unlock(lock);
	}

	closedir(dp);
	return (0);
}
