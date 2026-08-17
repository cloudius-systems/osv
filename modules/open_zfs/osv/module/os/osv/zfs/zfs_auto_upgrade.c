// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ZFS Automatic Pool Upgrade for OSv
 *
 * Provides automatic pool version upgrade on first mount.
 */

#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dsl_pool.h>
#include <sys/dmu_tx.h>
#include <sys/nvpair.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_context.h>

/* External option (defined in zfs_vfsops.c) */
extern int opt_zfs_auto_upgrade;

/*
 * Check if pool needs upgrade
 * Returns B_TRUE if pool version < SPA_VERSION (5000)
 */
static boolean_t
pool_needs_upgrade(spa_t *spa)
{
	uint64_t current_version;

	if (spa == NULL)
		return (B_FALSE);

	current_version = spa_version(spa);

	/* Check if already at latest version */
	if (current_version >= SPA_VERSION)
		return (B_FALSE);

	/* For legacy versions (< SPA_VERSION_FEATURES/5000), upgrade */
	if (current_version < SPA_VERSION_FEATURES) {
		printf("[ZFS] Pool '%s' at legacy version %llu, "
		    "upgrade recommended\n",
		    spa_name(spa), (unsigned long long)current_version);
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Safety checks before upgrading
 * Returns 0 if safe to upgrade, error code otherwise
 */
static int
check_upgrade_safety(spa_t *spa)
{
	uint64_t size, free_space;

	/* Check if pool is writable */
	if (!spa_writeable(spa)) {
		printf("[ZFS] Pool '%s' is read-only, skipping auto-upgrade\n",
		    spa_name(spa));
		return (SET_ERROR(EROFS));
	}

	/* Check pool state */
	if (spa_state(spa) != POOL_STATE_ACTIVE) {
		printf("[ZFS] Pool '%s' not active (state=%d), "
		    "skipping auto-upgrade\n",
		    spa_name(spa), spa_state(spa));
		return (SET_ERROR(ENXIO));
	}

	/* Check free space (require at least 1% free) */
	size = spa_get_dspace(spa);
	free_space = dsl_pool_adjustedsize(spa->spa_dsl_pool, B_FALSE);

	if (size > 0 && (free_space * 100) / size < 1) {
		printf("[ZFS] Pool '%s' has insufficient free space (< 1%%), "
		    "skipping auto-upgrade\n", spa_name(spa));
		return (SET_ERROR(ENOSPC));
	}

	/* Verify target version is supported */
	if (!SPA_VERSION_IS_SUPPORTED(SPA_VERSION)) {
		printf("[ZFS] Target version %llu not supported, "
		    "skipping auto-upgrade\n",
		    (unsigned long long)SPA_VERSION);
		return (SET_ERROR(ENOTSUP));
	}

	return (0);
}

/*
 * Perform automatic pool upgrade
 * Upgrades pool to SPA_VERSION (5000 - feature flags)
 */
static int
auto_upgrade_pool(const char *poolname)
{
	spa_t *spa = NULL;
	nvlist_t *nvprops;
	int error;

	printf("[ZFS] Attempting auto-upgrade of pool '%s' to version %llu\n",
	    poolname, (unsigned long long)SPA_VERSION);

	/* Open the pool */
	error = spa_open(poolname, &spa, FTAG);
	if (error != 0) {
		printf("[ZFS] Failed to open pool '%s' for upgrade: "
		    "error %d\n", poolname, error);
		return (error);
	}

	/* Check if upgrade needed */
	if (!pool_needs_upgrade(spa)) {
		printf("[ZFS] Pool '%s' is already at version %llu, "
		    "no upgrade needed\n",
		    poolname, (unsigned long long)spa_version(spa));
		spa_close(spa, FTAG);
		return (0);
	}

	/* Safety checks */
	error = check_upgrade_safety(spa);
	if (error != 0) {
		spa_close(spa, FTAG);
		return (error);
	}

	/* Prepare properties nvlist for upgrade */
	nvprops = fnvlist_alloc();
	fnvlist_add_uint64(nvprops, zpool_prop_to_name(ZPOOL_PROP_VERSION),
	    SPA_VERSION);

	/* Perform upgrade via spa_prop_set */
	printf("[ZFS] Upgrading pool '%s' from version %llu to %llu\n",
	    poolname,
	    (unsigned long long)spa_version(spa),
	    (unsigned long long)SPA_VERSION);

	error = spa_prop_set(spa, nvprops);
	fnvlist_free(nvprops);

	if (error != 0) {
		printf("[ZFS] Pool upgrade failed for '%s': error %d\n",
		    poolname, error);
	} else {
		printf("[ZFS] Pool '%s' upgraded successfully to version %llu\n",
		    poolname, (unsigned long long)SPA_VERSION);
	}

	spa_close(spa, FTAG);
	return (error);
}

/*
 * Hook called after pool import
 * This is the main entry point for auto-upgrade functionality
 */
void
zfs_post_import_hook(const char *poolname)
{
	spa_t *spa = NULL;
	boolean_t needs_upgrade;
	uint64_t current_version;
	int error;

	if (poolname == NULL)
		return;

	/* Check if auto-upgrade is enabled */
	if (!opt_zfs_auto_upgrade) {
		printf("[ZFS] Auto-upgrade disabled by configuration "
		    "for pool '%s'\n", poolname);
		return;
	}

	/* Open pool to check version */
	error = spa_open(poolname, &spa, FTAG);
	if (error != 0) {
		/* Pool not available, nothing to do */
		return;
	}

	/* Check if upgrade needed */
	needs_upgrade = pool_needs_upgrade(spa);
	current_version = spa_version(spa);
	spa_close(spa, FTAG);

	if (!needs_upgrade) {
		printf("[ZFS] Pool '%s' is up-to-date at version %llu\n",
		    poolname, (unsigned long long)current_version);
		return;
	}

	printf("[ZFS] Pool '%s' detected at version %llu, "
	    "auto-upgrading enabled\n",
	    poolname, (unsigned long long)current_version);

	/* Perform the upgrade */
	error = auto_upgrade_pool(poolname);
	if (error != 0) {
		printf("[ZFS] Auto-upgrade of pool '%s' failed with error %d\n",
		    poolname, error);
		printf("[ZFS] Pool can still be used at current version %llu\n",
		    (unsigned long long)current_version);
		printf("[ZFS] To disable auto-upgrade, use "
		    "--no-zfs-auto-upgrade boot option\n");
	}
}
