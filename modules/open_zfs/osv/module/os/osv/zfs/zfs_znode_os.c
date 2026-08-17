// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * Znode OS-specific operations for OSv.
 *
 * Manages the lifecycle of znodes, which are the in-memory
 * representation of ZFS file objects. On FreeBSD, znodes are
 * tightly coupled with vnodes via vhold/vrele. On OSv, we use
 * manual reference counting since OSv's vnode layer is simpler.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_dir.h>
#include <sys/vnode.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>

/*
 * Manual znode reference counting for OSv.
 * OSv does not have FreeBSD's vhold/vrele mechanism.
 */
void
zfs_zhold(znode_t *zp)
{
	atomic_inc_32(&zp->z_ref_cnt);
}

void
zfs_zrele(znode_t *zp)
{
	ASSERT3U(zp->z_ref_cnt, >, 0);
	if (atomic_dec_32_nv(&zp->z_ref_cnt) == 0) {
		/*
		 * Last reference dropped. The znode will be
		 * cleaned up by zfs_zinactive/zfs_znode_free.
		 */
	}
}

/*
 * zfs_znode_sa_init -- initialize the SA handle for a znode.
 *
 * Must be called with ZFS_OBJ_MUTEX held for the object.
 * Either creates a new SA handle from db, or attaches an existing one.
 */
void
zfs_znode_sa_init(zfsvfs_t *zfsvfs, znode_t *zp,
    dmu_buf_t *db, dmu_object_type_t obj_type, sa_handle_t *sa_hdl)
{
	ASSERT(MUTEX_HELD(ZFS_OBJ_MUTEX(zfsvfs, zp->z_id)));
	ASSERT0P(zp->z_sa_hdl);

	if (sa_hdl == NULL) {
		VERIFY0(sa_handle_get_from_db(zfsvfs->z_os, db, zp,
		    SA_HDL_SHARED, &zp->z_sa_hdl));
	} else {
		zp->z_sa_hdl = sa_hdl;
		sa_set_userp(sa_hdl, zp);
	}

	zp->z_is_sa = (obj_type == DMU_OT_SA) ? B_TRUE : B_FALSE;
}

/*
 * zfs_znode_dmu_fini -- destroy the SA handle for a znode.
 *
 * Must be called with ZFS_OBJ_MUTEX held for the object.
 */
void
zfs_znode_dmu_fini(znode_t *zp)
{
	ASSERT(MUTEX_HELD(ZFS_OBJ_MUTEX(zp->z_zfsvfs, zp->z_id)));
	ASSERT3P(zp->z_sa_hdl, !=, NULL);

	sa_handle_destroy(zp->z_sa_hdl);
	zp->z_sa_hdl = NULL;
}


/*
 * Callback invoked when acquiring a RL_WRITER or RL_APPEND lock on
 * z_rangelock.  Converts RL_APPEND to RL_WRITER at z_size, and
 * expands to the full file range if the block size might grow.
 */
static void
zfs_rangelock_cb(zfs_locked_range_t *new, void *arg)
{
	znode_t *zp = arg;

	if (new->lr_type == RL_APPEND) {
		new->lr_offset = zp->z_size;
		new->lr_type = RL_WRITER;
	}

	uint64_t end_size = MAX(zp->z_size, new->lr_offset + new->lr_length);
	if (zp->z_size <= zp->z_blksz && end_size > zp->z_blksz &&
	    (!ISP2(zp->z_blksz) || zp->z_blksz < ZTOZSB(zp)->z_max_blksz)) {
		new->lr_offset = 0;
		new->lr_length = UINT64_MAX;
	}
}

/*
 * Allocate and initialize a new znode.
 *
 * Must be called with ZFS_OBJ_MUTEX held (because zfs_znode_sa_init
 * requires it).  On success, *zpp is set and 0 is returned.  On
 * failure the dmu buffer reference passed in is NOT released (the
 * caller owns it and must call sa_buf_rele on error paths).
 */
static int
zfs_znode_alloc(zfsvfs_t *zfsvfs, dmu_buf_t *db, int blksz,
    dmu_object_type_t obj_type, sa_handle_t *hdl, znode_t **zpp)
{
	znode_t *zp;
	sa_bulk_attr_t bulk[9];
	uint64_t mode, parent;
	int count = 0;
	int error;

	zp = kmem_zalloc(sizeof (znode_t), KM_SLEEP);

	zp->z_sa_hdl = NULL;
	zp->z_unlinked = B_FALSE;
	zp->z_atime_dirty = B_FALSE;
	zp->z_mapcnt = 0;
	zp->z_id = db->db_object;
	zp->z_blksz = blksz;
	zp->z_seq = 0x7A4653;  /* "ZFS" in hex */
	zp->z_sync_cnt = 0;
	zp->z_ref_cnt = 1;
	zp->z_zfsvfs = zfsvfs;

	/* Initialize per-znode locks and the range lock. */
	mutex_init(&zp->z_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zp->z_acl_lock, NULL, MUTEX_DEFAULT, NULL);
	rw_init(&zp->z_xattr_lock, NULL, RW_DEFAULT, NULL);
	zfs_rangelock_init(&zp->z_rangelock, zfs_rangelock_cb, zp);

	/* Set up the SA handle and mark is_sa. */
	zfs_znode_sa_init(zfsvfs, zp, db, obj_type, hdl);

	/* Load the on-disk attributes we need to keep in the znode. */
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL, &mode, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GEN(zfsvfs), NULL,
	    &zp->z_gen, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &zp->z_size, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs), NULL,
	    &zp->z_links, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_PARENT(zfsvfs), NULL,
	    &parent, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL,
	    &zp->z_atime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
	    &zp->z_uid, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL,
	    &zp->z_gid, 8);

	error = sa_bulk_lookup(zp->z_sa_hdl, bulk, count);
	if (error != 0 || zp->z_gen == 0) {
		if (hdl == NULL)
			sa_handle_destroy(zp->z_sa_hdl);
		zp->z_sa_hdl = NULL;
		kmem_free(zp, sizeof (znode_t));
		return (error != 0 ? error : SET_ERROR(EIO));
	}

	zp->z_mode = mode;
	if (zp->z_pflags & ZFS_XATTR)
		zp->z_xattr_parent = parent;

	mutex_enter(&zfsvfs->z_znodes_lock);
	list_insert_tail(&zfsvfs->z_all_znodes, zp);
	mutex_exit(&zfsvfs->z_znodes_lock);

	*zpp = zp;
	return (0);
}

/*
 * Free a znode and remove it from the zfsvfs znode list.
 * The SA handle must have been destroyed (via zfs_znode_dmu_fini)
 * before calling this, or it is NULL.
 */
void
zfs_znode_free(znode_t *zp)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	mutex_enter(&zfsvfs->z_znodes_lock);
	list_remove(&zfsvfs->z_all_znodes, zp);
	mutex_exit(&zfsvfs->z_znodes_lock);

	if (zp->z_sa_hdl != NULL) {
		sa_handle_destroy(zp->z_sa_hdl);
		zp->z_sa_hdl = NULL;
	}

	/* Tear down per-znode locks. */
	zfs_rangelock_fini(&zp->z_rangelock);
	rw_destroy(&zp->z_xattr_lock);
	mutex_destroy(&zp->z_acl_lock);
	mutex_destroy(&zp->z_lock);

	kmem_free(zp, sizeof (znode_t));
}

/*
 * zfs_zinactive -- release a znode when the last VFS reference drops.
 *
 * Called from vop_inactive when the OSv vnode is being reclaimed.
 */
void
zfs_zinactive(znode_t *zp)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	uint64_t z_id = zp->z_id;

	ASSERT3P(zp->z_sa_hdl, !=, NULL);

	ZFS_OBJ_HOLD_ENTER(zfsvfs, z_id);
	zfs_znode_dmu_fini(zp);
	ZFS_OBJ_HOLD_EXIT(zfsvfs, z_id);
	zfs_znode_free(zp);
}

/*
 * zfs_zget -- look up or load a znode by object number.
 *
 * If the znode is already in memory (attached as SA userdata on the
 * DMU buffer), increment its reference count and return it.  If not,
 * allocate a new znode, set up its SA handle, and load its on-disk
 * attributes.
 *
 * On success *zpp is set to the znode and 0 is returned.
 */
int
zfs_zget(zfsvfs_t *zfsvfs, uint64_t obj_num, znode_t **zpp)
{
	dmu_object_info_t doi;
	dmu_buf_t *db;
	znode_t *zp;
	sa_handle_t *hdl;
	int err;

	*zpp = NULL;

	ZFS_OBJ_HOLD_ENTER(zfsvfs, obj_num);

	err = sa_buf_hold(zfsvfs->z_os, obj_num, NULL, &db);
	if (err != 0) {
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		return (err);
	}

	dmu_object_info_from_db(db, &doi);
	if (doi.doi_bonus_type != DMU_OT_SA &&
	    (doi.doi_bonus_type != DMU_OT_ZNODE ||
	    (doi.doi_bonus_type == DMU_OT_ZNODE &&
	    doi.doi_bonus_size < sizeof (znode_phys_t)))) {
		sa_buf_rele(db, NULL);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		return (SET_ERROR(EINVAL));
	}

	/* Check if there is already a live znode for this object. */
	hdl = dmu_buf_get_user(db);
	if (hdl != NULL) {
		zp = sa_get_userdata(hdl);

		ASSERT3P(zp, !=, NULL);
		ASSERT3U(zp->z_id, ==, obj_num);

		if (zp->z_unlinked) {
			err = SET_ERROR(ENOENT);
		} else {
			zfs_zhold(zp);
			*zpp = zp;
			err = 0;
		}

		sa_buf_rele(db, NULL);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		return (err);
	}

	/*
	 * No live znode found -- allocate one and load from disk.
	 * zfs_znode_alloc sets up the SA handle (which takes ownership
	 * of db) and loads the on-disk SA attributes.
	 */
	zp = NULL;
	err = zfs_znode_alloc(zfsvfs, db, doi.doi_data_block_size,
	    doi.doi_bonus_type, NULL, &zp);
	if (err != 0) {
		sa_buf_rele(db, NULL);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		return (err);
	}

	ASSERT3P(zp, !=, NULL);
	*zpp = zp;
	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
	return (0);
}

/*
 * Update VFS-cached attributes from the znode.
 * On OSv this is a no-op since we don't cache in the VFS layer.
 */
void
zfs_znode_update_vfs(znode_t *zp)
{
	(void) zp;
}

/*
 * zfs_znode_delete -- remove the DMU object backing a znode.
 *
 * Called on the error path of zfs_mkdir/zfs_create when the
 * directory-entry link step fails after zfs_mknode succeeded.
 *
 * The SA handle is destroyed and the underlying DMU object freed
 * inside the given transaction.  The caller must still call
 * zfs_znode_free() to remove the znode from z_all_znodes and
 * release the kmem allocation.
 */
void
zfs_znode_delete(znode_t *zp, dmu_tx_t *tx)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	uint64_t obj = zp->z_id;

	ZFS_OBJ_HOLD_ENTER(zfsvfs, obj);
	VERIFY0(dmu_object_free(zfsvfs->z_os, obj, tx));
	zfs_znode_dmu_fini(zp);
	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj);
}

/*
 * zfs_mknode -- allocate a new ZFS on-disk object and in-memory znode.
 *
 * Creates a new DMU object (ZAP for directories, plain for files),
 * initialises its SA attributes from vap and acl_ids, and returns
 * the new znode in *zpp.
 *
 * Must be called inside an open DMU transaction.
 * Caller holds no per-object mutex on entry (we acquire it here).
 */
void
zfs_mknode(znode_t *dzp, vattr_t *vap, dmu_tx_t *tx, cred_t *cr,
    uint_t flag, znode_t **zpp, zfs_acl_ids_t *acl_ids)
{
	uint64_t	crtime[2], atime[2], mtime[2], ctime[2];
	uint64_t	mode, size, links, parent, pflags;
	uint64_t	gen, obj;
	uint64_t	dacl_count = 0;
	int		bonuslen, dnodesize;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	dmu_buf_t	*db;
	timestruc_t	now;
	sa_handle_t	*sa_hdl;
	dmu_object_type_t obj_type;
	sa_bulk_attr_t	*sa_attrs;
	int		cnt = 0;
	zfs_acl_locator_cb_t locate = { 0 };

	(void) cr;

	ASSERT3P(vap, !=, NULL);

	gethrestime(&now);
	gen = dmu_tx_get_txg(tx);
	dnodesize = dmu_objset_dnodesize(zfsvfs->z_os);
	if (dnodesize == 0)
		dnodesize = DNODE_MIN_SIZE;

	obj_type = zfsvfs->z_use_sa ? DMU_OT_SA : DMU_OT_ZNODE;
	bonuslen = (obj_type == DMU_OT_SA) ?
	    DN_BONUS_SIZE(dnodesize) : ZFS_OLD_ZNODE_PHYS_SIZE;

	/*
	 * Allocate the DMU object.
	 */
	if (vap->va_type == VDIR) {
		obj = zap_create_norm_dnsize(zfsvfs->z_os,
		    zfsvfs->z_norm, DMU_OT_DIRECTORY_CONTENTS,
		    obj_type, bonuslen, dnodesize, tx);
	} else {
		obj = dmu_object_alloc_dnsize(zfsvfs->z_os,
		    DMU_OT_PLAIN_FILE_CONTENTS, 0,
		    obj_type, bonuslen, dnodesize, tx);
	}

	ZFS_OBJ_HOLD_ENTER(zfsvfs, obj);
	VERIFY0(sa_buf_hold(zfsvfs->z_os, obj, NULL, &db));

	if (flag & IS_ROOT_NODE)
		dzp->z_id = obj;

	/*
	 * Compute basic pflags.  OSv uses only ARCHIVE + AV_MODIFIED
	 * and marks every new object as having a trivial ACL.
	 */
	pflags = ZFS_ACL_TRIVIAL;
	if (zfsvfs->z_use_fuids)
		pflags |= ZFS_ARCHIVE | ZFS_AV_MODIFIED;

	if (vap->va_type == VDIR) {
		size = 2;		/* "." and ".." */
		links = (flag & (IS_ROOT_NODE | IS_XATTR)) ? 2 : 1;
	} else {
		size = links = 0;
	}

	parent = dzp->z_id;
	mode   = acl_ids->z_mode;

	ZFS_TIME_ENCODE(&now, crtime);
	ZFS_TIME_ENCODE(&now, ctime);
	ZFS_TIME_ENCODE(&now, atime);
	ZFS_TIME_ENCODE(&now, mtime);

	VERIFY0(sa_handle_get_from_db(zfsvfs->z_os, db, NULL,
	    SA_HDL_SHARED, &sa_hdl));

	sa_attrs = kmem_alloc(sizeof (sa_bulk_attr_t) * ZPL_END, KM_SLEEP);

	if (obj_type == DMU_OT_SA) {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MODE(zfsvfs),
		    NULL, &mode, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_SIZE(zfsvfs),
		    NULL, &size, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_GEN(zfsvfs),
		    NULL, &gen, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_UID(zfsvfs),
		    NULL, &acl_ids->z_fuid, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_GID(zfsvfs),
		    NULL, &acl_ids->z_fgid, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_PARENT(zfsvfs),
		    NULL, &parent, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_FLAGS(zfsvfs),
		    NULL, &pflags, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_ATIME(zfsvfs),
		    NULL, &atime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MTIME(zfsvfs),
		    NULL, &mtime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CTIME(zfsvfs),
		    NULL, &ctime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CRTIME(zfsvfs),
		    NULL, &crtime, 16);
	} else {
		/* DMU_OT_ZNODE: legacy znode_phys_t layout order */
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_ATIME(zfsvfs),
		    NULL, &atime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MTIME(zfsvfs),
		    NULL, &mtime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CTIME(zfsvfs),
		    NULL, &ctime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CRTIME(zfsvfs),
		    NULL, &crtime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_GEN(zfsvfs),
		    NULL, &gen, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MODE(zfsvfs),
		    NULL, &mode, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_SIZE(zfsvfs),
		    NULL, &size, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_PARENT(zfsvfs),
		    NULL, &parent, 8);
	}

	SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_LINKS(zfsvfs), NULL, &links, 8);

	if (obj_type == DMU_OT_SA) {
		/*
		 * Store DACL_COUNT = 0 (trivial ACL, no ACE entries).
		 * DACL_ACES is omitted when z_acl_bytes == 0.
		 */
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_DACL_COUNT(zfsvfs),
		    NULL, &dacl_count, 8);
		if (acl_ids->z_aclp->z_acl_bytes > 0) {
			locate.cb_aclp = acl_ids->z_aclp;
			SA_ADD_BULK_ATTR(sa_attrs, cnt,
			    SA_ZPL_DACL_ACES(zfsvfs),
			    zfs_acl_data_locator, &locate,
			    acl_ids->z_aclp->z_acl_bytes);
		}
	}

	VERIFY0(sa_replace_all_by_template(sa_hdl, sa_attrs, cnt, tx));
	kmem_free(sa_attrs, sizeof (sa_bulk_attr_t) * ZPL_END);

	if (!(flag & IS_ROOT_NODE)) {
		VERIFY0(zfs_znode_alloc(zfsvfs, db, 0, obj_type, sa_hdl, zpp));
	} else {
		/*
		 * Root node: the dzp IS the root znode; just attach the
		 * SA handle we just created.
		 */
		*zpp = dzp;
		(*zpp)->z_sa_hdl = sa_hdl;
		sa_set_userp(sa_hdl, dzp);
	}

	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj);
}

/*
 * Extend a file to 'end' bytes.  Expands block size if needed and
 * updates z_size + SA.
 */
static int
zfs_extend(znode_t *zp, uint64_t end)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	dmu_tx_t *tx;
	zfs_locked_range_t *lr;
	uint64_t newblksz;
	int error;

	lr = zfs_rangelock_enter(&zp->z_rangelock, 0, UINT64_MAX, RL_WRITER);

	if (end <= zp->z_size) {
		zfs_rangelock_exit(lr);
		return (0);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	if (end > zp->z_blksz &&
	    (!ISP2(zp->z_blksz) || zp->z_blksz < zfsvfs->z_max_blksz)) {
		if (zp->z_blksz > zp->z_zfsvfs->z_max_blksz) {
			ASSERT(!ISP2(zp->z_blksz));
			newblksz = MIN(end, 1 << highbit64(zp->z_blksz));
		} else {
			newblksz = MIN(end, zp->z_zfsvfs->z_max_blksz);
		}
		dmu_tx_hold_write(tx, zp->z_id, 0, newblksz);
	} else {
		newblksz = 0;
	}

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_rangelock_exit(lr);
		return (error);
	}

	if (newblksz)
		zfs_grow_blocksize(zp, newblksz, tx);

	zp->z_size = end;
	VERIFY0(sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zp->z_zfsvfs),
	    &zp->z_size, sizeof (zp->z_size), tx));
	/*
	 * A znode loaded via zfs_zget() outside the VFS vget() path (e.g. during
	 * ZIL replay at mount, before any vnode is attached) has z_vnode == NULL,
	 * so ZTOV(zp) is NULL.  vnode_pager_setsize() only updates the page-cache
	 * size hint (vp->v_size); there is no vnode/page cache to size for a
	 * replay znode, so skip it (z_size + SA are already authoritative).
	 */
	if (ZTOV(zp) != NULL)
		vnode_pager_setsize(ZTOV(zp), end);

	zfs_rangelock_exit(lr);
	dmu_tx_commit(tx);
	return (0);
}

/*
 * Free a byte range within a file.
 */
static int
zfs_free_range(znode_t *zp, uint64_t off, uint64_t len)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	zfs_locked_range_t *lr;
	int error;

	lr = zfs_rangelock_enter(&zp->z_rangelock, off, len, RL_WRITER);

	if (off >= zp->z_size) {
		zfs_rangelock_exit(lr);
		return (0);
	}

	if (off + len > zp->z_size)
		len = zp->z_size - off;

	error = dmu_free_long_range(zfsvfs->z_os, zp->z_id, off, len);
	/* z_vnode may be NULL for a replay-loaded znode; see zfs_extend(). */
	if (error == 0 && ZTOV(zp) != NULL)
		vnode_pager_setsize(ZTOV(zp), off);

	zfs_rangelock_exit(lr);
	return (error);
}

/*
 * Truncate a file to 'end' bytes.
 */
static int
zfs_trunc(znode_t *zp, uint64_t end)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	vnode_t *vp = ZTOV(zp);
	dmu_tx_t *tx;
	zfs_locked_range_t *lr;
	int error;
	sa_bulk_attr_t bulk[2];
	int count = 0;

	lr = zfs_rangelock_enter(&zp->z_rangelock, 0, UINT64_MAX, RL_WRITER);

	if (end >= zp->z_size) {
		zfs_rangelock_exit(lr);
		return (0);
	}

	error = dmu_free_long_range(zfsvfs->z_os, zp->z_id, end,
	    DMU_OBJECT_END);
	if (error) {
		zfs_rangelock_exit(lr);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_rangelock_exit(lr);
		return (error);
	}

	zp->z_size = end;
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs),
	    NULL, &zp->z_size, sizeof (zp->z_size));
	if (end == 0) {
		zp->z_pflags &= ~ZFS_SPARSE;
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
		    NULL, &zp->z_pflags, 8);
	}
	VERIFY0(sa_bulk_update(zp->z_sa_hdl, bulk, count, tx));
	dmu_tx_commit(tx);

	/* z_vnode may be NULL for a replay-loaded znode; see zfs_extend(). */
	if (vp != NULL)
		vnode_pager_setsize(vp, end);
	zfs_rangelock_exit(lr);
	return (0);
}

/*
 * Free space in a file - implements ftruncate(2) for ZFS on OSv.
 *
 *	IN:	zp	- znode of file to free data in.
 *		off	- start of range (new EOF when len == 0)
 *		len	- length (0 means truncate to off)
 *		flag	- current file open mode flags (unused on OSv)
 *		log	- TRUE if this action should be logged
 *
 *	RETURN:	0 on success, error code on failure
 */
int
zfs_freesp(znode_t *zp, uint64_t off, uint64_t len, int flag, boolean_t log)
{
	dmu_tx_t *tx;
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	zilog_t *zilog = zfsvfs->z_log;
	uint64_t mode;
	uint64_t mtime[2], ctime[2];
	sa_bulk_attr_t bulk[3];
	int count = 0;
	int error;

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_MODE(zfsvfs), &mode,
	    sizeof (mode))) != 0)
		return (error);

	if (off > zp->z_size) {
		error = zfs_extend(zp, off + len);
		if (error == 0 && log)
			goto log;
		else
			return (error);
	}

	if (len == 0) {
		error = zfs_trunc(zp, off);
	} else {
		if ((error = zfs_free_range(zp, off, len)) == 0 &&
		    off + len > zp->z_size)
			error = zfs_extend(zp, off + len);
	}
	if (error || !log)
		return (error);
log:
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
	    NULL, &zp->z_pflags, 8);
	zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);
	error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);

	zfs_log_truncate(zilog, tx, TX_TRUNCATE, zp, off, len);

	dmu_tx_commit(tx);
	return (0);
}
