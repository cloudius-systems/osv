// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ZFS directory operations for OSv.
 * Provides directory lookup, create, and management functions.
 */

#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_sa.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_pool.h>
#include <sys/dmu_tx.h>
#include <sys/sa.h>
#include <sys/zap.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>

/*
 * Timestamp update setup for create/modify operations.
 */
void
zfs_tstamp_update_setup_ext(znode_t *zp, uint_t flag, uint64_t mtime[2],
    uint64_t ctime[2], boolean_t have_tx)
{
	(void) have_tx;
	zfs_tstamp_update_setup(zp, flag, mtime, ctime);
}

void
zfs_tstamp_update_setup(znode_t *zp, uint_t flag, uint64_t mtime[2],
    uint64_t ctime[2])
{
	timestruc_t now;

	gethrestime(&now);

	if (flag & CONTENT_MODIFIED) {
		ZFS_TIME_ENCODE(&now, mtime);
	}

	if (flag & STATE_CHANGED) {
		ZFS_TIME_ENCODE(&now, ctime);
	}
}

/*
 * Grow the block size of a file.
 */
void
zfs_grow_blocksize(znode_t *zp, uint64_t size, dmu_tx_t *tx)
{
	int error;
	uint64_t newblksz;
	zfsvfs_t *zfsvfs = ZTOZSB(zp);

	if (size <= zp->z_blksz)
		return;

	/*
	 * If the file size is already greater than the full block size,
	 * no need to grow.
	 */
	if (zp->z_blksz == zfsvfs->z_max_blksz)
		return;

	newblksz = MIN(size, zfsvfs->z_max_blksz);
	newblksz = MAX(newblksz, SPA_MINBLOCKSIZE);
	newblksz = ISP2(newblksz) ? newblksz : (1ULL << highbit64(newblksz));
	if (newblksz > zfsvfs->z_max_blksz)
		newblksz = zfsvfs->z_max_blksz;

	error = dmu_object_set_blocksize(zfsvfs->z_os, zp->z_id, newblksz,
	    0, tx);

	if (error == ENOTSUP)
		return;
	if (error == 0)
		zp->z_blksz = newblksz;
}

/*
 * Compressed device encoding.
 * OSv: Simply cast to dev_t (uint64_t).
 */
dev_t
zfs_cmpldev(uint64_t dev)
{
	return ((dev_t)dev);
}

/*
 * Create a new ZFS filesystem (dataset).
 *
 * Initialises the on-disk ZPL structures:
 *   - master node ZAP (object 1)
 *   - SA attr-registration ZAP (if SA version)
 *   - delete queue ZAP
 *   - root directory znode (DMU object, SA attributes)
 *   - ZFS_ROOT_OBJ entry in master node
 *
 * This is called from dsl_pool / zfs_ioctl during "zpool create".
 */
void
zfs_create_fs(objset_t *os, cred_t *cr, nvlist_t *zplprops, dmu_tx_t *tx)
{
	uint64_t	moid, obj, sa_obj, version;
	uint64_t	norm = 0, sense = ZFS_CASE_SENSITIVE;
	nvpair_t	*elem;
	int		error;
	int		i;
	zfsvfs_t	*zfsvfs;
	znode_t		*rootzp;
	vattr_t		vattr;
	znode_t		*zp;
	zfs_acl_ids_t	acl_ids;
	sa_attr_type_t	*sa_table = NULL;

	/*
	 * Create master node.
	 */
	moid = MASTER_NODE_OBJ;
	error = zap_create_claim(os, moid, DMU_OT_MASTER_NODE,
	    DMU_OT_NONE, 0, tx);
	VERIFY0(error);

	/*
	 * Determine version and other ZPL properties.
	 */
	version = zplprops ? fnvlist_lookup_uint64(zplprops,
	    zfs_prop_to_name(ZFS_PROP_VERSION)) : ZPL_VERSION;
	elem = NULL;
	while ((elem = nvlist_next_nvpair(zplprops, elem)) != NULL) {
		uint64_t val = fnvpair_value_uint64(elem);
		const char *name = nvpair_name(elem);
		if (strcmp(name, zfs_prop_to_name(ZFS_PROP_VERSION)) == 0) {
			if (val < version)
				version = val;
		} else {
			error = zap_update(os, moid, name, 8, 1, &val, tx);
		}
		VERIFY0(error);
		if (strcmp(name, zfs_prop_to_name(ZFS_PROP_NORMALIZE)) == 0)
			norm = val;
		else if (strcmp(name, zfs_prop_to_name(ZFS_PROP_CASE)) == 0)
			sense = val;
	}
	error = zap_update(os, moid, ZPL_VERSION_STR, 8, 1, &version, tx);
	VERIFY0(error);

	/*
	 * Create SA attr-registration ZAP for modern (SA-capable) pools.
	 */
	if (USE_SA(version, os)) {
		sa_obj = zap_create(os, DMU_OT_SA_MASTER_NODE,
		    DMU_OT_NONE, 0, tx);
		error = zap_add(os, moid, ZFS_SA_ATTRS, 8, 1, &sa_obj, tx);
		VERIFY0(error);
	} else {
		sa_obj = 0;
	}

	/*
	 * Create delete queue (unlinked set).
	 */
	obj = zap_create(os, DMU_OT_UNLINKED_SET, DMU_OT_NONE, 0, tx);
	error = zap_add(os, moid, ZFS_UNLINKED_SET, 8, 1, &obj, tx);
	VERIFY0(error);

	/*
	 * Create root directory znode.
	 *
	 * We build a minimal zfsvfs_t and root znode in order to drive
	 * zfs_mknode().  These are ephemeral structures that exist only
	 * for the duration of this call and are discarded afterwards.
	 */
	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);
	zfsvfs->z_os = os;
	zfsvfs->z_parent = zfsvfs;
	zfsvfs->z_version = version;
	zfsvfs->z_use_fuids = USE_FUIDS(version, os);
	zfsvfs->z_use_sa = USE_SA(version, os);
	zfsvfs->z_norm = norm;
	if (sense == ZFS_CASE_INSENSITIVE || sense == ZFS_CASE_MIXED)
		zfsvfs->z_norm |= U8_TEXTPREP_TOUPPER;

	/* Need the SA attr table so zfs_mknode can use SA_ZPL_* macros. */
	VERIFY0(sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &sa_table));
	zfsvfs->z_attr_table = sa_table;

	mutex_init(&zfsvfs->z_znodes_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zfsvfs->z_all_znodes, sizeof (znode_t),
	    offsetof(znode_t, z_link_node));
	for (i = 0; i < ZFS_OBJ_MTX_SZ; i++)
		mutex_init(&zfsvfs->z_hold_mtx[i], NULL, MUTEX_DEFAULT, NULL);

	rootzp = kmem_zalloc(sizeof (znode_t), KM_SLEEP);
	rootzp->z_zfsvfs = zfsvfs;
	rootzp->z_unlinked = B_FALSE;
	rootzp->z_atime_dirty = B_FALSE;
	rootzp->z_is_sa = zfsvfs->z_use_sa;
	rootzp->z_pflags = 0;
	rootzp->z_ref_cnt = 1;

	vattr.va_mask = AT_MODE | AT_UID | AT_GID | AT_TYPE;
	vattr.va_type = VDIR;
	vattr.va_mode = S_IFDIR | 0755;
	vattr.va_uid = 0;
	vattr.va_gid = 0;

	VERIFY0(zfs_acl_ids_create(rootzp, IS_ROOT_NODE, &vattr,
	    cr, NULL, &acl_ids, NULL));
	zfs_mknode(rootzp, &vattr, tx, cr, IS_ROOT_NODE, &zp, &acl_ids);
	ASSERT3P(zp, ==, rootzp);

	error = zap_add(os, moid, ZFS_ROOT_OBJ, 8, 1, &rootzp->z_id, tx);
	VERIFY0(error);

	zfs_acl_ids_free(&acl_ids);

	/* Tear down ephemeral structures. */
	if (rootzp->z_sa_hdl != NULL)
		sa_handle_destroy(rootzp->z_sa_hdl);
	kmem_free(rootzp, sizeof (znode_t));

	sa_tear_down(os);
	zfsvfs->z_attr_table = NULL;

	for (i = 0; i < ZFS_OBJ_MTX_SZ; i++)
		mutex_destroy(&zfsvfs->z_hold_mtx[i]);
	list_destroy(&zfsvfs->z_all_znodes);
	mutex_destroy(&zfsvfs->z_znodes_lock);
	kmem_free(zfsvfs, sizeof (zfsvfs_t));
}

/*
 * Extended attribute directory creation stub.
 */
int
zfs_make_xattrdir(znode_t *zp, vattr_t *vap, znode_t **xzpp, cred_t *cr)
{
	(void) zp; (void) vap; (void) xzpp; (void) cr;
	return (SET_ERROR(ENOTSUP));
}

/*
 * zfs_match_find -- look up a name in a ZAP directory.
 *
 * Handles both case-sensitive and case-insensitive (z_norm) filesystems.
 * On success, *zoid is set to ZFS_DIRENT_OBJ(raw_value) and 0 returned.
 */
static int
zfs_match_find(zfsvfs_t *zfsvfs, znode_t *dzp, const char *name,
    matchtype_t mt, uint64_t *zoid)
{
	int error;

	if (zfsvfs->z_norm) {
		error = zap_lookup_norm(zfsvfs->z_os, dzp->z_id, name, 8, 1,
		    zoid, mt, NULL, 0, NULL);
	} else {
		error = zap_lookup(zfsvfs->z_os, dzp->z_id, name, 8, 1, zoid);
	}
	*zoid = ZFS_DIRENT_OBJ(*zoid);
	return (error);
}

/*
 * zfs_dirent_lookup -- find a znode by name in a directory.
 *
 * Flags:
 *   ZNEW    -- fail with EEXIST if the entry already exists.
 *   ZEXISTS -- fail with ENOENT if the entry does not exist.
 *   ZXATTR  -- look for the xattr directory instead of a normal entry.
 *
 * On success with ZEXISTS, *zpp is the found znode.
 * On success with ZNEW, *zpp is NULL.
 */
int
zfs_dirent_lookup(znode_t *dzp, const char *name, znode_t **zpp, int flag)
{
	zfsvfs_t *zfsvfs = ZTOZSB(dzp);
	znode_t *zp;
	matchtype_t mt = 0;
	uint64_t zoid;
	int error = 0;

	*zpp = NULL;

	if (zfsvfs->z_norm != 0) {
		mt = MT_NORMALIZE;
		if (zfsvfs->z_case == ZFS_CASE_MIXED)
			mt |= MT_MATCH_CASE;
	}

	if (dzp->z_unlinked && !(flag & ZXATTR))
		return (SET_ERROR(ENOENT));

	if (flag & ZXATTR) {
		error = sa_lookup(dzp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs), &zoid,
		    sizeof (zoid));
		if (error == 0)
			error = (zoid == 0 ? ENOENT : 0);
	} else {
		error = zfs_match_find(zfsvfs, dzp, name, mt, &zoid);
	}

	if (error) {
		if (error != ENOENT || (flag & ZEXISTS))
			return (error);
		/* ENOENT and !ZEXISTS: entry absent - OK for ZNEW */
		return (0);
	} else {
		if (flag & ZNEW)
			return (SET_ERROR(EEXIST));
		/* Entry found; load the znode. */
		error = zfs_zget(zfsvfs, zoid, &zp);
		if (error != 0)
			return (error);
		*zpp = zp;
	}

	return (0);
}


static uint64_t
zfs_dirent(znode_t *zp, uint64_t mode)
{
	uint64_t de = zp->z_id;
	zfsvfs_t *zfsvfs = ZTOZSB(zp);

	if (zfsvfs->z_version >= ZPL_VERSION_DIRENT_TYPE)
		de |= IFTODT(mode) << 60;
	return (de);
}

/*
 * zfs_link_create -- add a ZAP directory entry linking name to zp.
 *
 * Updates the parent directory size/links and the child's link count
 * and ctime.  Must be called inside a DMU transaction.
 */
int
zfs_link_create(znode_t *dzp, const char *name, znode_t *zp, dmu_tx_t *tx,
    int flag)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	uint64_t value;
	/*
	 * Use S_ISDIR(z_mode) rather than ZTOV(zp)->v_type because on OSv
	 * znodes created via zfs_mkdir() are not immediately attached to a VFS
	 * vnode (z_vnode == NULL), so ZTOV(zp) returns NULL here.  The mode
	 * bits in z_mode are always populated from the SA at allocation time
	 * and are the authoritative source for the object type.
	 */
	int zp_is_dir = S_ISDIR(zp->z_mode);
	sa_bulk_attr_t bulk[5];
	uint64_t mtime[2], ctime[2];
	int count = 0;
	int error;

	if (zp_is_dir) {
		if (dzp->z_links >= ZFS_LINK_MAX)
			return (SET_ERROR(EMLINK));
	}

	if (!(flag & ZRENAMING)) {
		if (zp->z_unlinked)
			return (SET_ERROR(ENOENT));
		if (zp->z_links >= ZFS_LINK_MAX - zp_is_dir)
			return (SET_ERROR(EMLINK));
		zp->z_links++;
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs), NULL,
		    &zp->z_links, sizeof (zp->z_links));
	}

	value = zfs_dirent(zp, zp->z_mode);
	error = zap_add(zfsvfs->z_os, dzp->z_id, name, 8, 1, &value, tx);
	if (error != 0) {
		if (!(flag & ZRENAMING) && !(flag & ZNEW))
			zp->z_links--;
		return (error);
	}

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_PARENT(zfsvfs), NULL,
	    &dzp->z_id, sizeof (dzp->z_id));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));

	if (!(flag & ZNEW)) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    ctime, sizeof (ctime));
		zfs_tstamp_update_setup(zp, STATE_CHANGED, mtime, ctime);
	}
	error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);

	dzp->z_size++;
	dzp->z_links += zp_is_dir;
	count = 0;
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &dzp->z_size, sizeof (dzp->z_size));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs), NULL,
	    &dzp->z_links, sizeof (dzp->z_links));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
	    mtime, sizeof (mtime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
	    ctime, sizeof (ctime));
	zfs_tstamp_update_setup(dzp, CONTENT_MODIFIED, mtime, ctime);
	error = sa_bulk_update(dzp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);

	return (0);
}

/*
 * Remove a ZAP directory entry.  Handles normalisation if needed.
 */
static int
zfs_dropname(znode_t *dzp, const char *name, znode_t *zp, dmu_tx_t *tx,
    int flag)
{
	int error;
	(void) flag;

	if (zp->z_zfsvfs->z_norm) {
		matchtype_t mt = MT_NORMALIZE;
		if (zp->z_zfsvfs->z_case == ZFS_CASE_MIXED)
			mt |= MT_MATCH_CASE;
		error = zap_remove_norm(zp->z_zfsvfs->z_os, dzp->z_id,
		    name, mt, tx);
	} else {
		error = zap_remove(zp->z_zfsvfs->z_os, dzp->z_id, name, tx);
	}
	return (error);
}

/*
 * Indicate whether the directory is empty.
 * A ZFS directory with only "." and ".." has z_size == 2.
 */
boolean_t
zfs_dirempty(znode_t *dzp)
{
	return (dzp->z_size == 2);
}

/*
 * Add zp to the unlinked set (to be reclaimed on next mount).
 */
void
zfs_unlinked_add(znode_t *zp, dmu_tx_t *tx)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	ASSERT(zp->z_unlinked);
	ASSERT0(zp->z_links);

	VERIFY0(zap_add_int(zfsvfs->z_os, zfsvfs->z_unlinkedobj, zp->z_id, tx));
	dataset_kstats_update_nunlinks_kstat(&zfsvfs->z_kstat, 1);
}

/*
 * Unlink zp from dzp, and mark zp for deletion if this was the last link.
 * Can fail with ENOTEMPTY if zp is a non-empty directory.
 * If unlinkedp is non-NULL the caller manages the unlinked-set; otherwise
 * we call zfs_unlinked_add() ourselves.
 */
int
zfs_link_destroy(znode_t *dzp, const char *name, znode_t *zp, dmu_tx_t *tx,
    int flag, boolean_t *unlinkedp)
{
	zfsvfs_t *zfsvfs = dzp->z_zfsvfs;
	/*
	 * Use S_ISDIR(z_mode) rather than ZTOV(zp)->v_type: on OSv a znode
	 * may have z_vnode == NULL when loaded via zfs_zget() outside the
	 * VFS vget() path, making ZTOV(zp) return NULL.  z_mode is always
	 * authoritative for the object type.
	 */
	int zp_is_dir = S_ISDIR(zp->z_mode);
	boolean_t unlinked = B_FALSE;
	sa_bulk_attr_t bulk[5];
	uint64_t mtime[2], ctime[2];
	int count = 0;
	int error;

	if (!(flag & ZRENAMING)) {
		if (zp_is_dir && !zfs_dirempty(zp))
			return (SET_ERROR(ENOTEMPTY));

		error = zfs_dropname(dzp, name, zp, tx, flag);
		if (error != 0)
			return (error);

		if (zp->z_links <= (uint64_t)zp_is_dir) {
			zfs_panic_recover("zfs: link count on vnode %p is %u, "
			    "should be at least %u", (void *)zp->z_vnode,
			    (int)zp->z_links, zp_is_dir + 1);
			zp->z_links = zp_is_dir + 1;
		}
		if (--zp->z_links == (uint64_t)zp_is_dir) {
			zp->z_unlinked = B_TRUE;
			zp->z_links = 0;
			unlinked = B_TRUE;
		} else {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs),
			    NULL, &ctime, sizeof (ctime));
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
			    NULL, &zp->z_pflags, sizeof (zp->z_pflags));
			zfs_tstamp_update_setup(zp, STATE_CHANGED, mtime,
			    ctime);
		}
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs),
		    NULL, &zp->z_links, sizeof (zp->z_links));
		error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		count = 0;
		ASSERT0(error);
	} else {
		ASSERT(!zp->z_unlinked);
		error = zfs_dropname(dzp, name, zp, tx, flag);
		if (error != 0)
			return (error);
	}

	dzp->z_size--;
	dzp->z_links -= zp_is_dir;
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs),
	    NULL, &dzp->z_links, sizeof (dzp->z_links));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs),
	    NULL, &dzp->z_size, sizeof (dzp->z_size));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs),
	    NULL, ctime, sizeof (ctime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs),
	    NULL, mtime, sizeof (mtime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
	    NULL, &dzp->z_pflags, sizeof (dzp->z_pflags));
	zfs_tstamp_update_setup(dzp, CONTENT_MODIFIED, mtime, ctime);
	error = sa_bulk_update(dzp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);

	if (unlinkedp != NULL)
		*unlinkedp = unlinked;
	else if (unlinked)
		zfs_unlinked_add(zp, tx);

	return (0);
}

void
zfs_znode_init(void)
{
	/* Nothing needed on OSv */
}

void
zfs_znode_fini(void)
{
	/* Nothing needed on OSv */
}

/*
 * Drop the last hold on a znode taken by zfs_zget() outside the VFS, as
 * zfs_vop_inactive() drops a vnode binding's: under the object hold, so a
 * concurrent zfs_zget() cannot re-grab it in between.  An unlinked znode
 * cannot be re-grabbed (zfs_zget() refuses it), so it is released after the
 * hold is dropped, through zfs_zinactive(), which frees its space.
 */
static void
zfs_zrele_async_task(void *arg)
{
	znode_t *zp = arg;
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	uint64_t z_id = zp->z_id;

	ZFS_OBJ_HOLD_ENTER(zfsvfs, z_id);
	if (atomic_dec_32_nv(&zp->z_ref_cnt) != 0) {
		ZFS_OBJ_HOLD_EXIT(zfsvfs, z_id);
		return;
	}
	if (zp->z_unlinked && zp->z_sa_hdl != NULL) {
		ZFS_OBJ_HOLD_EXIT(zfsvfs, z_id);
		zfs_zinactive(zp);
		return;
	}
	if (zp->z_sa_hdl != NULL)
		zfs_znode_dmu_fini(zp);
	ZFS_OBJ_HOLD_EXIT(zfsvfs, z_id);
	zfs_znode_free(zp);
}

/*
 * Release a hold from a context that must not tear the znode down inline:
 * zfs_get_data() calls this with the txg held open, and freeing an unlinked
 * file needs a transaction.  Like Linux, drop the hold in place unless it is
 * the last one, and hand the last one to the pool's zrele taskq.
 * zfs_osv_unmount() waits for that taskq before it tears the znodes down.
 */
void
zfs_zrele_async(znode_t *zp)
{
	objset_t *os = zp->z_zfsvfs->z_os;
	uint32_t n;

	for (;;) {
		n = zp->z_ref_cnt;
		ASSERT3U(n, >, 0);
		if (n == 1)
			break;
		if (atomic_cas_32(&zp->z_ref_cnt, n, n - 1) == n)
			return;
	}
	VERIFY(taskq_dispatch(dsl_pool_zrele_taskq(dmu_objset_pool(os)),
	    zfs_zrele_async_task, zp, TQ_SLEEP) != TASKQID_INVALID);
}

/*
 * Drain the unlinked set at mount: free every object that lost its last
 * link while still open when the pool was last exported, crashed, or
 * unmounted.
 *
 * Each entry is loaded with zfs_zget(), marked unlinked, and released
 * through zfs_zinactive(), which frees it with zfs_rmnode().  An entry
 * that is already live in memory (zfs_zget() returns ENOENT for an
 * unlinked live znode) belongs to its current holder and is skipped.
 */
void
zfs_unlinked_drain(zfsvfs_t *zfsvfs)
{
	zap_cursor_t	zc;
	zap_attribute_t *zap;
	dmu_object_info_t doi;
	znode_t		*zp;
	dmu_tx_t	*tx;
	int		error;

	zap = zap_attribute_alloc();
	for (zap_cursor_init(&zc, zfsvfs->z_os, zfsvfs->z_unlinkedobj);
	    zap_cursor_retrieve(&zc, zap) == 0;
	    zap_cursor_advance(&zc)) {
		error = dmu_object_info(zfsvfs->z_os,
		    zap->za_first_integer, &doi);
		if (error != 0)
			continue;

		error = zfs_zget(zfsvfs, zap->za_first_integer, &zp);
		if (error != 0)
			continue;

		/* zfs_rmnode() requires a zero link count on disk. */
		if (zp->z_links != 0) {
			tx = dmu_tx_create(zfsvfs->z_os);
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
			error = dmu_tx_assign(tx, DMU_TX_WAIT);
			if (error != 0) {
				dmu_tx_abort(tx);
				zfs_zinactive(zp);
				continue;
			}
			zp->z_links = 0;
			VERIFY0(sa_update(zp->z_sa_hdl, SA_ZPL_LINKS(zfsvfs),
			    &zp->z_links, sizeof (zp->z_links), tx));
			dmu_tx_commit(tx);
		}

		zp->z_unlinked = B_TRUE;
		zfs_zinactive(zp);
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(zap);
}

/*
 * Free the data and the object of a file whose last link is gone, after
 * its last reference was dropped, and remove it from the unlinked set.
 *
 * On any failure (no space for the final transaction, or an interrupted
 * free) the object stays in the unlinked set and the next mount's
 * zfs_unlinked_drain() retries it.  Either way the znode is freed.
 *
 * OSv cannot create extended-attribute directories (zfs_make_xattrdir()
 * returns ENOTSUP), so there is no xattr directory to purge here; one
 * found on a pool written elsewhere is moved to the unlinked set, as
 * FreeBSD does.  shortcut: OSv never purges such a directory's entries, so
 * they leak on pools imported from FreeBSD/Linux with xattr=dir files;
 * port zfs_purgedir() if that ever matters.
 */
void
zfs_rmnode(znode_t *zp)
{
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	objset_t	*os = zfsvfs->z_os;
	dmu_tx_t	*tx;
	uint64_t	z_id = zp->z_id;
	uint64_t	acl_obj;
	uint64_t	xattr_obj;
	uint64_t	count;
	int		error;

	ASSERT0(zp->z_links);

	/* Free the file data first, in as many transactions as it takes. */
	error = dmu_free_long_range(os, z_id, 0, DMU_OBJECT_END);
	if (error != 0)
		goto keep;

	error = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
	    &xattr_obj, sizeof (xattr_obj));
	if (error != 0)
		xattr_obj = 0;

	acl_obj = zfs_external_acl(zp);

	tx = dmu_tx_create(os);
	dmu_tx_hold_free(tx, z_id, 0, DMU_OBJECT_END);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	if (xattr_obj)
		dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, TRUE, NULL);
	if (acl_obj)
		dmu_tx_hold_free(tx, acl_obj, 0, DMU_OBJECT_END);
	zfs_sa_upgrade_txholds(tx, zp);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
		goto keep;
	}

	if (xattr_obj) {
		VERIFY0(zap_add_int(os, zfsvfs->z_unlinkedobj,
		    xattr_obj, tx));
	}

	mutex_enter(&os->os_dsl_dataset->ds_dir->dd_activity_lock);
	VERIFY0(zap_remove_int(os, zfsvfs->z_unlinkedobj, z_id, tx));
	if (zap_count(os, zfsvfs->z_unlinkedobj, &count) == 0 && count == 0)
		cv_broadcast(&os->os_dsl_dataset->ds_dir->dd_activity_cv);
	mutex_exit(&os->os_dsl_dataset->ds_dir->dd_activity_lock);

	dataset_kstats_update_nunlinked_kstat(&zfsvfs->z_kstat, 1);

	zfs_znode_delete(zp, tx);
	zfs_znode_free(zp);
	dmu_tx_commit(tx);
	return;

keep:
	ZFS_OBJ_HOLD_ENTER(zfsvfs, z_id);
	zfs_znode_dmu_fini(zp);
	ZFS_OBJ_HOLD_EXIT(zfsvfs, z_id);
	zfs_znode_free(zp);
}

/*
 * Update the zfsvfs name after a rename.
 */
void
zfsvfs_update_fromname(const char *oldname, const char *newname)
{
	(void) oldname; (void) newname;
}

/*
 * Get the unmounted flag for a VFS.
 */
boolean_t
zfs_get_vfs_flag_unmounted(objset_t *os)
{
	(void) os;
	return (B_FALSE);
}
