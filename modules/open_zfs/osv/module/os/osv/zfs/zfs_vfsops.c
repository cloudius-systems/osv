// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ZFS VFS operations for OSv.
 *
 * Provides mount/unmount and filesystem lifecycle management.
 * Many of these functions parallel the common code in module/zfs/
 * but with OSv-specific adaptations.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/zfs_context.h>
#include <osv/mount.h>
#include <fs/vfs/vfs_id.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dir.h>
#include <sys/spa.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/sa.h>
#include <sys/fs/zfs.h>
#include <zfs_comutil.h>

int zfs_super_owner = 0;

/* ZFS auto-upgrade option (default enabled; loader.cc may override via CONF_libzfs) */
int opt_zfs_auto_upgrade = 1;

/*
 * Active filesystem count. Used by zfs_busy() to prevent
 * module unload while filesystems are mounted.
 */
static uint32_t zfs_active_fs_count = 0;

int
zfs_busy(void)
{
	return (zfs_active_fs_count != 0);
}

/*
 * Create a zfsvfs structure for the given dataset.
 */
int
zfsvfs_create(const char *osname, boolean_t readonly, zfsvfs_t **zfvp)
{
	objset_t *os;
	zfsvfs_t *zfsvfs;
	int error;

	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);

	error = dmu_objset_own(osname,
	    DMU_OST_ZFS, readonly ? B_TRUE : B_FALSE, B_TRUE,
	    zfsvfs, &os);
	if (error != 0) {
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	/*
	 * Call auto-upgrade hook after successfully opening dataset.
	 * Extract pool name from dataset name (everything before first '/')
	 * This will check and upgrade the pool if needed.
	 */
	if (error == 0) {
		extern void zfs_post_import_hook(const char *poolname);

		/* Extract pool name from osname (e.g., "mypool/dataset" -> "mypool") */
		char poolname[256];
		const char *slash = strchr(osname, '/');
		if (slash) {
			size_t len = slash - osname;
			if (len < sizeof(poolname)) {
				memcpy(poolname, osname, len);
				poolname[len] = '\0';
				zfs_post_import_hook(poolname);
			}
		} else {
			/* osname is the pool name itself */
			zfs_post_import_hook(osname);
		}
	}

	error = zfsvfs_create_impl(zfvp, zfsvfs, os);
	if (error != 0) {
		dmu_objset_disown(os, B_TRUE, zfsvfs);
		zfsvfs_free(zfsvfs);
	}
	return (error);
}

/*
 * zfsvfs_init -- read on-disk ZPL properties into a zfsvfs_t.
 *
 * Reads the master-node ZAP to populate all fields that affect
 * VOP behaviour: version, SA attr table, root object, unlinked set,
 * normalization, case handling, and feature flags.
 *
 * Must be called after the objset is open and before any ZFS I/O.
 */
static int
zfsvfs_init(zfsvfs_t *zfsvfs, objset_t *os)
{
	int		error;
	uint64_t	val;
	uint64_t	sa_obj = 0;

	zfsvfs->z_max_blksz = SPA_OLD_MAXBLOCKSIZE;
	zfsvfs->z_os = os;

	error = zfs_get_zplprop(os, ZFS_PROP_VERSION, &zfsvfs->z_version);
	if (error != 0)
		return (error);

	error = zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_norm = (int)val;

	error = zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_utf8 = (val != 0);

	error = zfs_get_zplprop(os, ZFS_PROP_CASE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_case = (uint_t)val;

	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE ||
	    zfsvfs->z_case == ZFS_CASE_MIXED)
		zfsvfs->z_norm |= U8_TEXTPREP_TOUPPER;

	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, os);
	zfsvfs->z_use_sa    = USE_SA(zfsvfs->z_version, os);

	if (zfsvfs->z_use_sa) {
		error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1,
		    &sa_obj);
		if (error != 0)
			return (error);
	}

	error = sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &zfsvfs->z_attr_table);
	if (error != 0)
		return (error);

	if (zfsvfs->z_version >= ZPL_VERSION_SA)
		sa_register_update_callback(os, zfs_sa_upgrade);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ, 8, 1,
	    &zfsvfs->z_root);
	if (error != 0)
		return (error);
	ASSERT3U(zfsvfs->z_root, !=, 0);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_UNLINKED_SET, 8, 1,
	    &zfsvfs->z_unlinkedobj);
	if (error != 0)
		return (error);

	/* Quota objects are optional - tolerate ENOENT. */
	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA],
	    8, 1, &zfsvfs->z_userquota_obj);
	if (error == ENOENT)
		zfsvfs->z_userquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA],
	    8, 1, &zfsvfs->z_groupquota_obj);
	if (error == ENOENT)
		zfsvfs->z_groupquota_obj = 0;
	else if (error != 0)
		return (error);

	return (0);
}

/*
 * Implementation of zfsvfs creation from an already-opened objset.
 *
 * On error the caller is responsible for calling zfsvfs_free().
 */
int
zfsvfs_create_impl(zfsvfs_t **zfvp, zfsvfs_t *zfsvfs, objset_t *os)
{
	int error;

	zfsvfs->z_os = os;
	zfsvfs->z_parent = zfsvfs;

	mutex_init(&zfsvfs->z_znodes_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zfsvfs->z_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zfsvfs->z_all_znodes, sizeof (znode_t),
	    offsetof(znode_t, z_link_node));

	ZFS_TEARDOWN_INIT(zfsvfs);
	ZFS_TEARDOWN_INACTIVE_INIT(zfsvfs);

	for (int i = 0; i < ZFS_OBJ_MTX_SZ; i++)
		mutex_init(&zfsvfs->z_hold_mtx[i], NULL, MUTEX_DEFAULT, NULL);

	error = zfsvfs_init(zfsvfs, os);
	if (error != 0)
		return (error);

	*zfvp = zfsvfs;
	return (0);
}

/*
 * Free a zfsvfs structure.
 */
void
zfsvfs_free(zfsvfs_t *zfsvfs)
{
	for (int i = 0; i < ZFS_OBJ_MTX_SZ; i++)
		mutex_destroy(&zfsvfs->z_hold_mtx[i]);

	ZFS_TEARDOWN_DESTROY(zfsvfs);
	ZFS_TEARDOWN_INACTIVE_DESTROY(zfsvfs);

	list_destroy(&zfsvfs->z_all_znodes);
	mutex_destroy(&zfsvfs->z_znodes_lock);
	mutex_destroy(&zfsvfs->z_lock);

	kmem_free(zfsvfs, sizeof (zfsvfs_t));
}

/*
 * Check if the zfsvfs is read-only.
 */
boolean_t
zfs_is_readonly(zfsvfs_t *zfsvfs)
{
	/*
	 * Reflect the dataset's readonly property (read at mount time in
	 * zfs_domount and stored in zfsvfs->z_readonly), plus
	 * snapshots which are always read-only.
	 */
	return (zfsvfs->z_readonly || zfsvfs->z_issnap);
}

/*
 * Suspend/resume for pool operations.
 */
int
zfs_suspend_fs(zfsvfs_t *zfsvfs)
{
	ZFS_TEARDOWN_ENTER_WRITE(zfsvfs, FTAG);
	return (0);
}

int
zfs_resume_fs(zfsvfs_t *zfsvfs, dsl_dataset_t *ds)
{
	int err;

	err = dmu_objset_find_dp(spa_get_dsl(dsl_dataset_get_spa(ds)),
	    dsl_dir_phys(ds->ds_dir)->dd_child_dir_zapobj,
	    NULL, NULL, DS_FIND_CHILDREN);

	ZFS_TEARDOWN_EXIT_WRITE(zfsvfs);
	return (err);
}

int
zfs_end_fs(zfsvfs_t *zfsvfs, dsl_dataset_t *ds)
{
	(void) ds;
	ZFS_TEARDOWN_EXIT_WRITE(zfsvfs);
	return (0);
}

/*
 * Set the ZPL version on the filesystem.
 */
int
zfs_set_version(zfsvfs_t *zfsvfs, uint64_t newvers)
{
	int error;
	objset_t *os = zfsvfs->z_os;
	dmu_tx_t *tx;

	if (newvers < ZPL_VERSION_INITIAL || newvers > ZPL_VERSION)
		return (SET_ERROR(EINVAL));

	if (newvers < zfsvfs->z_version)
		return (SET_ERROR(EINVAL));

	if (zfs_spa_version_map(newvers) >
	    spa_version(dmu_objset_spa(zfsvfs->z_os)))
		return (SET_ERROR(ENOTSUP));

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_FALSE, ZPL_VERSION_STR);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	error = zap_update(os, MASTER_NODE_OBJ, ZPL_VERSION_STR,
	    8, 1, &newvers, tx);
	if (error == 0) {
		spa_history_log_internal(dmu_objset_spa(os), "upgrade", tx,
		    "from %lu to %lu", (unsigned long)zfsvfs->z_version,
		    (unsigned long)newvers);
	}

	dmu_tx_commit(tx);
	zfsvfs->z_version = newvers;

	return (error);
}

/*
 * Global label check (security feature, always passes on OSv).
 */
int
zfs_check_global_label(const char *dsname, const char *hexsl)
{
	(void) dsname;
	(void) hexsl;
	return (0);
}

/*
 * Get temporary property value for dataset.
 */
int
zfs_get_temporary_prop(dsl_dataset_t *ds, zfs_prop_t zfs_prop,
    uint64_t *val, char *setpoint)
{
	(void) ds;
	(void) zfs_prop;
	(void) val;
	(void) setpoint;
	return (SET_ERROR(ENOENT));
}

/*
 * ZFS init/fini -- called during kernel startup/shutdown.
 */
void
zfs_init(void)
{
	zfs_znode_init();
}

void
zfs_fini(void)
{
	zfs_znode_fini();
}

/* ------------------------------------------------------------------ */
/* OSv VFS mount/unmount/sync/statfs                                   */
/* ------------------------------------------------------------------ */

/*
 * These are declared in zfs_vnops_os.c and the null_vfsops.cc wrapper.
 * We reference zfs_vnops here so we can embed a pointer in zfs_osv_vfsops.
 */
extern struct vnops zfs_vnops;

/*
 * Set the FUID feature flags on the zfsvfs based on the ZPL version
 * and on-disk features.
 */
static void
zfs_set_fuid_feature(zfsvfs_t *zfsvfs)
{
	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);
}

/*
 * OSv implementation of zfsvfs_setup().
 *
 * On FreeBSD/Linux this registers property-change callbacks and then
 * replays the ZIL.  OSv has no dsl_prop callback infrastructure yet,
 * so we skip property registration and perform only the steps that are
 * required for the filesystem to be usable:
 *
 *   1. Open the ZIL.
 *   2. Drain the unlinked set (on writable mounts).
 *   3. Replay the intent log.
 *   4. Associate the zfsvfs with the objset user pointer.
 *
 * TODO: add dsl_prop_register() calls when OSv property callbacks are
 * implemented.
 */
static int
zfsvfs_setup(zfsvfs_t *zfsvfs, boolean_t mounting)
{
	int error;

	if (mounting) {
		error = dataset_kstats_create(&zfsvfs->z_kstat, zfsvfs->z_os);
		if (error)
			return (error);

		zfsvfs->z_log = zil_open(zfsvfs->z_os, zfs_get_data,
		    &zfsvfs->z_kstat.dk_zil_sums);

		if (!zfs_is_readonly(zfsvfs)) {
			zap_stats_t zs;
			if (zap_get_stats(zfsvfs->z_os, zfsvfs->z_unlinkedobj,
			    &zs) == 0) {
				dataset_kstats_update_nunlinks_kstat(
				    &zfsvfs->z_kstat, zs.zs_num_entries);
			}
			zfs_unlinked_drain(zfsvfs);
			dsl_dir_t *dd =
			    zfsvfs->z_os->os_dsl_dataset->ds_dir;
			dd->dd_activity_cancelled = B_FALSE;
		}

		/*
		 * Replay the intent log.
		 */
		if (spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
			if (zil_replay_disable) {
				zil_destroy(zfsvfs->z_log, B_FALSE);
			} else {
				zfsvfs->z_replay = B_TRUE;
				zil_replay(zfsvfs->z_os, zfsvfs,
				    zfs_replay_vector);
				zfsvfs->z_replay = B_FALSE;
			}
		}
	} else {
		ASSERT3P(zfsvfs->z_kstat.dk_kstats, !=, NULL);
		zfsvfs->z_log = zil_open(zfsvfs->z_os, zfs_get_data,
		    &zfsvfs->z_kstat.dk_zil_sums);
	}

	/*
	 * Associate the zfsvfs with the objset so that callbacks
	 * (e.g. from spa_sync) can find us.
	 */
	mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
	dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
	mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);

	return (0);
}

/*
 * zfs_domount() -- mount a ZFS dataset onto the OSv VFS mount point.
 *
 * This is the OSv equivalent of the FreeBSD/Linux zfs_domount().
 * It initialises a zfsvfs_t for the named dataset and wires it into
 * the OSv struct mount (vfs_t).
 *
 * Parameters:
 *   mp     - OSv VFS mount structure (vfs_t == struct mount)
 *   osname - ZFS dataset name (e.g. "pool/data")
 *
 * On success, mp->m_data points to the live zfsvfs_t and the
 * filesystem is ready for I/O.  On failure all resources are freed.
 */
int
zfs_domount(struct mount *mp, const char *osname)
{
	zfsvfs_t *zfsvfs;
	uint64_t fsid_guid;
	int error;

	ASSERT3P(mp, !=, NULL);
	ASSERT3P(osname, !=, NULL);

	/*
	 * Build and populate the zfsvfs_t.  zfsvfs_create() opens the
	 * objset and initialises mutexes, the znode list, and teardown
	 * locks.
	 */
	error = zfsvfs_create(osname, (mp->vfs_flag & VFS_RDONLY) != 0,
	    &zfsvfs);
	if (error)
		return (error);

	zfsvfs->z_vfs = mp;

	/*
	 * Wire the private data pointer so that the VFS ops (sync,
	 * statfs, unmount) can reach the zfsvfs_t.
	 */
	mp->vfs_data = zfsvfs;

	/*
	 * Build a 64-bit filesystem ID from the objset GUID.
	 *
	 * OSv's struct mount uses m_fsid (== vfs_fsid via the macro in
	 * vfs.h), which is a fsid_t { int32_t val[2]; }.
	 *
	 * st_dev is reassembled as:
	 *   st_dev = (uint32_t)__val[0] | ((uint32_t)__val[1] << 32)
	 *
	 * We do NOT set ZFS_ID in the upper byte.  OSv's pagecache routes
	 * IS_ZFS() files to the ARC bridge path which requires arc_share_buf()
	 * wrappers not available in OpenZFS 2.x.  By leaving ZFS_ID clear,
	 * ZFS files use the regular read_cache path (same as ROFS) which is
	 * fed by zfs_vop_cache() → zfs_read() → ARC internally.
	 */
	fsid_guid = dmu_objset_fsid_guid(zfsvfs->z_os);
	mp->m_fsid.__val[0] = (int32_t)(fsid_guid & 0xFFFFFFFF);
	mp->m_fsid.__val[1] = (int32_t)(fsid_guid >> 32);

	/*
	 * Set feature flags (FUID / system-attributes) based on the
	 * on-disk ZPL version.
	 */
	zfs_set_fuid_feature(zfsvfs);

	if (dmu_objset_is_snapshot(zfsvfs->z_os)) {
		/*
		 * Snapshots are always read-only.  Disable sync and
		 * link the zfsvfs to the objset.
		 */
		zfsvfs->z_issnap = B_TRUE;
		zfsvfs->z_atime = B_FALSE;
		zfsvfs->z_os->os_sync = ZFS_SYNC_DISABLED;

		mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
		dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
		mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);
	} else {
		/*
		 * Regular dataset: open the ZIL, drain unlinked set,
		 * replay the intent log.
		 */
		error = zfsvfs_setup(zfsvfs, B_TRUE);
		if (error)
			goto out;
	}

	/*
	 * Honor the dataset's readonly property.  OSv's VFS never passed the
	 * ZFS readonly property through to the mount, so writes to a
	 * readonly=on dataset were silently allowed.  Read it here and record
	 * it in z_readonly so zfs_is_readonly() (checked by the write vnops)
	 * enforces it.
	 */
	{
		uint64_t ro = 0;
		if (dsl_prop_get_integer(osname, "readonly", &ro, NULL) == 0 &&
		    ro != 0)
			zfsvfs->z_readonly = B_TRUE;
	}

	atomic_inc_32(&zfs_active_fs_count);
	return (0);

out:
	dmu_objset_disown(zfsvfs->z_os, B_TRUE, zfsvfs);
	zfsvfs_free(zfsvfs);
	mp->vfs_data = NULL;
	return (error);
}

static int
zfs_osv_mount(struct mount *mp, const char *dev, int flags, const void *data)
{
	/*
	 * OSv calls mount_rootfs("/zfs", "/dev/vblk0.1", "zfs", 0, "osv")
	 * so: dev = block device path, data = dataset name (osname).
	 *
	 * When data is non-NULL it is the dataset name and dev is the raw
	 * device; import the pool from the device first, then mount the
	 * dataset.  When data is NULL (e.g. remount or explicit mount where
	 * the pool is already imported), dev is the dataset name directly.
	 */
	const char *osname;
	int error = 0;

	/*
	 * Distinguish two calling conventions:
	 *
	 * 1. Root boot (loader.cc): dev = "/dev/vblk0.1", data = "osv"
	 *    The pool is not yet imported; import it from the device, then
	 *    mount the dataset named by data.
	 *
	 * 2. libzfs (do_mount): dev = "osv", data = "" or options string
	 *    The pool is already imported; dev IS the dataset name.
	 *
	 * Use the presence of "/dev/" prefix in dev to tell them apart.
	 */
	if (strncmp(dev, "/dev/", 5) == 0 &&
	    data != NULL && ((const char *)data)[0] != '\0') {
		/* Case 1: root boot */
		osname = (const char *)data;
		error = spa_import_rootpool(dev, B_FALSE);
		if (error) {
			printf("zfs_mount: spa_import_rootpool(%s) failed: %d\n", dev, error);
			return (error);
		}
	} else {
		/* Case 2: pool already imported; dev is the dataset name */
		osname = dev;
	}

	error = zfs_domount(mp, osname);
	if (error) {
		printf("zfs_mount: zfs_domount(%s) failed: %d\n", osname, error);
	}
	if (error)
		return (error);

	/*
	 * OSv's sys_mount() creates the root vnode (via vget) *before*
	 * calling VFS_MOUNT, so vp->v_data is still NULL at that point.
	 * Now that zfs_domount() has set up zfsvfs, wire the root znode
	 * to the root vnode so that all subsequent VOPs work correctly.
	 */
	zfsvfs_t *zfsvfs = mp->m_data;
	if (zfsvfs != NULL && mp->m_root != NULL) {
		struct vnode *vp = mp->m_root->d_vnode;
		if (vp != NULL && vp->v_data == NULL) {
			znode_t *zp = NULL;
			int enter_err = zfs_enter(zfsvfs, FTAG);
			if (enter_err == 0) {
				int zget_err = zfs_zget(zfsvfs, zfsvfs->z_root, &zp);
				if (zget_err == 0) {
					vp->v_data = zp;
					vp->v_ino = zp->z_id;
					zp->z_vnode = vp;
					printf("ZFS: root mounted ok, z_root=%llu\n",
					    (unsigned long long)zfsvfs->z_root);
				} else {
					printf("zfs_mount: zfs_zget(root=%llu) failed: %d\n",
					    (unsigned long long)zfsvfs->z_root, zget_err);
				}
				zfs_exit(zfsvfs, FTAG);
			}
		}
	}

	return (0);
}

static int
zfs_osv_unmount(struct mount *mp, int flags)
{
	(void) flags;
	zfsvfs_t *zfsvfs = mp->m_data;
	int error;

	if (zfsvfs == NULL)
		return (0);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	/*
	 * Close the ZIL to flush any pending log writes before
	 * disowning the objset.  Without this, in-flight ZIOs may
	 * still hold a key_mapping reference, causing
	 * spa_keystore_unload_wkey_impl() to fail with EBUSY.
	 */
	if (zfsvfs->z_log != NULL) {
		zil_close(zfsvfs->z_log);
		zfsvfs->z_log = NULL;
	}

	/*
	 * Sync the pool to ensure all pending transactions (and
	 * their encryption ZIOs) have committed.  This drains any
	 * remaining key_mapping refcounts held by in-flight ZIOs.
	 */
	txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);

	/*
	 * Release the root znode before disowning the objset.
	 *
	 * zfs_mount() takes a hold on the root znode (zfs_zget of
	 * zfsvfs->z_root) and wires it to the mount's root vnode.  On
	 * FreeBSD/Linux the generic VFS reclaims that vnode -- running
	 * vop_inactive, which destroys the znode's SA handle -- before
	 * VFS_UNMOUNT is called.  OSv's VFS does not, so without this the
	 * root znode keeps its bonus-buffer (dnode) hold.  That hold pins
	 * the shared dnode-block dbuf, so its async eviction never fires,
	 * the objset's os_dnodes list never drains, and a later
	 * spa_export's spa_evicting_os_wait() blocks forever.  Drop it
	 * here exactly as vop_inactive would on the last reference.
	 */
	struct vnode *rootvp =
	    (mp->m_root != NULL) ? mp->m_root->d_vnode : NULL;
	if (rootvp != NULL && rootvp->v_data != NULL) {
		znode_t *rzp = VTOZ(rootvp);
		rootvp->v_data = NULL;
		rzp->z_vnode = NULL;
		if (rzp->z_sa_hdl != NULL)
			zfs_zinactive(rzp);
		else
			zfs_znode_free(rzp);
	}

	/*
	 * Drain every other live znode before disowning the objset.
	 *
	 * The mount root is not the only znode OSv keeps live: any directory
	 * or file touched during the mount lifetime (e.g. the root ZAP
	 * directory walked while writing files, or cached leaf vnodes) gets a
	 * znode whose SA handle holds the object's bonus buffer.  This is
	 * especially visible for a child dataset such as pool/fs, whose own
	 * objset accumulates such znodes.  FreeBSD/Linux reclaim every vnode
	 * via vop_inactive before VFS_UNMOUNT; OSv's VFS only force-drops the
	 * mount root above, leaving the rest live.  Each surviving bonus hold
	 * pins its dnode, so dnode_destroy never runs, the objset's os_dnodes
	 * list never empties, and the subsequent spa_export's
	 * spa_evicting_os_wait() blocks forever.  Inactivate them all here,
	 * exactly as vop_inactive would on the last reference.
	 */
	mutex_enter(&zfsvfs->z_znodes_lock);
	for (znode_t *zp = list_head(&zfsvfs->z_all_znodes);
	    zp != NULL; zp = list_head(&zfsvfs->z_all_znodes)) {
		struct vnode *zvp = zp->z_vnode;
		if (zvp != NULL)
			zvp->v_data = NULL;
		zp->z_vnode = NULL;
		/*
		 * zfs_zinactive()/zfs_znode_free() remove zp from
		 * z_all_znodes, so drop the lock across the call and reload
		 * the list head each iteration.
		 */
		mutex_exit(&zfsvfs->z_znodes_lock);
		if (zp->z_sa_hdl != NULL)
			zfs_zinactive(zp);
		else
			zfs_znode_free(zp);
		mutex_enter(&zfsvfs->z_znodes_lock);
	}
	mutex_exit(&zfsvfs->z_znodes_lock);

	dmu_objset_disown(zfsvfs->z_os, B_TRUE, zfsvfs);

	zfs_exit(zfsvfs, FTAG);
	zfsvfs_free(zfsvfs);
	mp->m_data = NULL;
	return (0);
}

static int
zfs_osv_sync(struct mount *mp)
{
	zfsvfs_t *zfsvfs = mp->m_data;
	int error;

	if (zfsvfs == NULL)
		return (0);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

static int
zfs_osv_vget(struct mount *mp, struct vnode *vp)
{
	zfsvfs_t *zfsvfs = mp->m_data;
	znode_t *zp = NULL;
	int error;

	/*
	 * This is called by vget() for non-root vnodes that are not yet
	 * in the vnode cache.  mp->m_data may be NULL if called before
	 * VFS_MOUNT (e.g. for the root vnode during sys_mount); in that
	 * case we return 0 and let zfs_osv_mount wire the root znode.
	 */
	if (zfsvfs == NULL || vp->v_ino == 0)
		return (0);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	error = zfs_zget(zfsvfs, vp->v_ino, &zp);
	if (error == 0 && zp != NULL) {
		vp->v_data = zp;
		vp->v_ino  = zp->z_id;
		zp->z_vnode = vp;
		vp->v_mode = zp->z_mode;
		vp->v_type = IFTOVT(zp->z_mode);
		vp->v_size = zp->z_size;
	}

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

static int
zfs_osv_statfs(struct mount *mp, struct statfs *statp)
{
	zfsvfs_t *zfsvfs = mp->m_data;
	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	int error;

	if (zfsvfs == NULL)
		return (SET_ERROR(EIO));

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	dmu_objset_space(zfsvfs->z_os,
	    &refdbytes, &availbytes, &usedobjs, &availobjs);

	statp->f_bsize  = SPA_MINBLOCKSIZE;
	statp->f_frsize = statp->f_bsize;
	statp->f_blocks = (refdbytes + availbytes) >> SPA_MINBLOCKSHIFT;
	statp->f_bfree  = availbytes >> SPA_MINBLOCKSHIFT;
	statp->f_bavail = statp->f_bfree;
	statp->f_files  = (fsfilcnt_t)usedobjs;
	statp->f_ffree  = (fsfilcnt_t)availobjs;

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * Set a default quota property on this filesystem.
 * Mirrors the Linux/FreeBSD implementation.
 */
int
zfs_set_default_quota(zfsvfs_t *zfsvfs, zfs_prop_t prop, uint64_t quota)
{
	int error;
	objset_t *os = zfsvfs->z_os;
	const char *propstr = zfs_prop_to_name(prop);
	dmu_tx_t *tx;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_FALSE, propstr);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	if (quota == 0) {
		error = zap_remove(os, MASTER_NODE_OBJ, propstr, tx);
		if (error == ENOENT)
			error = 0;
	} else {
		error = zap_update(os, MASTER_NODE_OBJ, propstr, 8, 1,
		    &quota, tx);
	}

	if (error)
		goto out;

	switch (prop) {
	case ZFS_PROP_DEFAULTUSERQUOTA:
		zfsvfs->z_defaultuserquota = quota;
		break;
	case ZFS_PROP_DEFAULTGROUPQUOTA:
		zfsvfs->z_defaultgroupquota = quota;
		break;
	case ZFS_PROP_DEFAULTPROJECTQUOTA:
		zfsvfs->z_defaultprojectquota = quota;
		break;
	case ZFS_PROP_DEFAULTUSEROBJQUOTA:
		zfsvfs->z_defaultuserobjquota = quota;
		break;
	case ZFS_PROP_DEFAULTGROUPOBJQUOTA:
		zfsvfs->z_defaultgroupobjquota = quota;
		break;
	case ZFS_PROP_DEFAULTPROJECTOBJQUOTA:
		zfsvfs->z_defaultprojectobjquota = quota;
		break;
	default:
		break;
	}

out:
	dmu_tx_commit(tx);
	return (error);
}

/*
 * The real ZFS VFS operations - registered via zfs_update_vfsops()
 * when libsolaris.so is loaded.
 */
struct vfsops zfs_osv_vfsops = {
	zfs_osv_mount,		/* mount   */
	zfs_osv_unmount,	/* unmount */
	zfs_osv_sync,		/* sync    */
	zfs_osv_vget,		/* vget    */
	zfs_osv_statfs,		/* statfs  */
	&zfs_vnops,		/* vnops   */
};
