// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * SPA (Storage Pool Allocator) OS-specific functions for OSv.
 *
 * Provides root pool import and pool lifecycle hooks.
 * Based on the FreeBSD spa_os.c but adapted for OSv's device model.
 */

#include <sys/zfs_context.h>
#include <sys/fm/fs/zfs.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/zap.h>
#include <sys/vdev_impl.h>
#include <sys/metaslab.h>
#include <sys/uberblock_impl.h>
#include <sys/txg.h>
#include <sys/avl.h>
#include <sys/unique.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/fs/zfs.h>
#include <sys/arc.h>
#include <sys/zfeature.h>

/*
 * Forward declaration -- vdev_disk_read_rootlabel is provided by vdev_disk.c
 */
extern int vdev_disk_read_rootlabel(char *devpath, nvlist_t **config);

static nvlist_t *
spa_generate_rootconf(const char *name)
{
	nvlist_t *config = NULL;
	nvlist_t *nvtop, *nvroot;
	uint64_t pgid;

	/*
	 * Read label from the root device.
	 * OSv's root device path is typically /dev/vblk0.
	 */
	if (vdev_disk_read_rootlabel((char *)name, &config) != 0)
		return (NULL);

	/*
	 * Build a root vdev config from the label.
	 */
	nvtop = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);

	pgid = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID);
	nvroot = fnvlist_alloc();
	fnvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT);
	fnvlist_add_uint64(nvroot, ZPOOL_CONFIG_ID, 0ULL);
	fnvlist_add_uint64(nvroot, ZPOOL_CONFIG_GUID, pgid);
	fnvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t * const *)&nvtop, 1);

	fnvlist_add_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, nvroot);
	fnvlist_free(nvroot);

	return (config);
}

int
spa_import_rootpool(const char *name, bool checkpointrewind)
{
	spa_t *spa;
	vdev_t *rvd;
	nvlist_t *config, *nvtop;
	const char *pname;
	int error;

	config = spa_generate_rootconf(name);

	spa_namespace_enter(FTAG);
	if (config != NULL) {
		pname = fnvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME);

		if ((spa = spa_lookup(pname)) != NULL) {
			if (spa->spa_state == POOL_STATE_ACTIVE) {
				spa_namespace_exit(FTAG);
				fnvlist_free(config);
				return (0);
			}
			spa_remove(spa);
		}
		spa = spa_add(pname, config, NULL);

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION,
		    &spa->spa_ubsync.ub_version) != 0)
			spa->spa_ubsync.ub_version = SPA_VERSION_INITIAL;
	} else if ((spa = spa_lookup(name)) == NULL) {
		spa_namespace_exit(FTAG);
		cmn_err(CE_NOTE, "Cannot find the pool label for '%s'", name);
		return (EIO);
	} else {
		config = fnvlist_dup(spa->spa_config);
	}

	spa->spa_is_root = B_TRUE;
	spa->spa_import_flags = ZFS_IMPORT_VERBATIM;
	if (checkpointrewind)
		spa->spa_import_flags |= ZFS_IMPORT_CHECKPOINT;

	nvtop = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);
	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	error = spa_config_parse(spa, &rvd, nvtop, NULL, 0,
	    VDEV_ALLOC_ROOTPOOL);
	spa_config_exit(spa, SCL_ALL, FTAG);
	if (error) {
		spa_namespace_exit(FTAG);
		fnvlist_free(config);
		cmn_err(CE_NOTE, "Can not parse the config for pool '%s'",
		    name);
		return (error);
	}

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	vdev_free(rvd);
	spa_config_exit(spa, SCL_ALL, FTAG);
	spa_namespace_exit(FTAG);

	fnvlist_free(config);
	return (0);
}

const char *
spa_history_zone(void)
{
	return ("osv");
}

void
spa_import_os(spa_t *spa)
{
	(void) spa;
}

void
spa_export_os(spa_t *spa)
{
	(void) spa;
}

void
spa_activate_os(spa_t *spa)
{
	(void) spa;
}

void
spa_deactivate_os(spa_t *spa)
{
	(void) spa;
}
