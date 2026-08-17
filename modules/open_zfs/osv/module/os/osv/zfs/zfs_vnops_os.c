// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ZFS vnode operations for OSv.
 *
 * Bridges OSv's struct vnops dispatch table to the platform-independent
 * OpenZFS vnode operations in module/zfs/zfs_vnops.c.
 *
 * Key design point: OSv is a unikernel with a single address space.
 * O_DIRECT I/O works by passing user buffer page addresses directly to
 * the ABD layer - no page pinning is required.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/sa.h>
#include <sys/zfs_sa.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <osv/file.h>
#include <osv/mount.h>
#include <fs/vfs/vfs_id.h>
#include <sys/zap.h>
#include <sys/zfs_dir.h>

/*
 * OS-specific vnode operations.
 *
 * NOTE: Do not redefine functions that exist in the common
 * zfs_vnops.c module, as they will cause linker errors.
 *
 * All functions here are kernel-internal stubs and must NOT be exported
 * from libsolaris.so - several names (e.g. zfs_create, zfs_rename) clash
 * with identically-named functions in libzfs.so (the userspace management
 * library).  Marking the whole file hidden keeps them out of the .dynsym
 * table while still allowing intra-library calls from zfs_replay.c etc.
 */
#pragma GCC visibility push(hidden)

int
zfs_remove(znode_t *dzp, const char *name, cred_t *cr, int flags)
{
	(void) cr; (void) flags;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	znode_t		*zp;
	dmu_tx_t	*tx;
	boolean_t	unlinked;
	int		error;

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);

	error = zfs_dirent_lookup(dzp, name, &zp, ZEXISTS);
	if (error != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (ZTOTYPE(zp) == VDIR) {
		zfs_zrele(zp);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	dmu_tx_mark_netfree(tx);

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
		zfs_zrele(zp);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	error = zfs_link_destroy(dzp, name, zp, tx, ZEXISTS, &unlinked);
	if (error == 0 && unlinked)
		zfs_unlinked_add(zp, tx);

	dmu_tx_commit(tx);
	zfs_zrele(zp);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * zfs_create -- create a regular file in a ZFS directory.
 *
 * OSv simplified version: no FUIDs, no ZIL logging, no quota checks.
 *
 * If excl == EXCL and the entry already exists, returns EEXIST.
 * If excl != EXCL and the entry already exists, returns the existing
 * znode in *zpp (caller must release with zfs_zrele).
 */
int
zfs_create(znode_t *dzp, const char *name, vattr_t *vap, int excl,
    int mode, znode_t **zpp, cred_t *cr, int flag, vsecattr_t *vsecp,
    zidmap_t *mnt_ns)
{
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	znode_t		*zp = NULL;
	zfs_acl_ids_t	acl_ids;
	dmu_tx_t	*tx;
	int		error;

	(void) mode; (void) flag;

	*zpp = NULL;

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);

	vap->va_type = VREG;
	vap->va_mask |= AT_TYPE;

	error = zfs_dirent_lookup(dzp, name, &zp, excl ? ZNEW : 0);
	if (error != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (zp != NULL) {
		/* File already exists and caller did not request EXCL. */
		*zpp = zp;
		zfs_exit(zfsvfs, FTAG);
		return (0);
	}

	if ((error = zfs_acl_ids_create(dzp, 0, vap, cr, vsecp,
	    &acl_ids, mnt_ns)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa_create(tx, ZFS_SA_BASE_ATTR_SIZE);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error != 0) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	error = zfs_link_create(dzp, name, zp, tx, ZNEW);
	if (error != 0) {
		zfs_znode_delete(zp, tx);
		zfs_znode_free(zp);
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_commit(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	zfs_acl_ids_free(&acl_ids);
	dmu_tx_commit(tx);

	*zpp = zp;
	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * zfs_mkdir -- create a directory in a ZFS directory.
 *
 * OSv simplified version: no FUIDs, no ZIL logging, no quota checks.
 */
int
zfs_mkdir(znode_t *dzp, const char *dirname, vattr_t *vap,
    znode_t **zpp, cred_t *cr, int flags, vsecattr_t *vsecp,
    zidmap_t *mnt_ns)
{
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	znode_t		*zp = NULL;
	zfs_acl_ids_t	acl_ids;
	dmu_tx_t	*tx;
	int		error;

	(void) flags;

	*zpp = NULL;

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);

	vap->va_type = VDIR;
	vap->va_mask |= AT_TYPE;

	/* Fail if entry already exists. */
	if ((error = zfs_dirent_lookup(dzp, dirname, &zp, ZNEW)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	ASSERT0P(zp);

	if ((error = zfs_acl_ids_create(dzp, 0, vap, cr, vsecp,
	    &acl_ids, mnt_ns)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, dirname);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	dmu_tx_hold_sa_create(tx, ZFS_SA_BASE_ATTR_SIZE);

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error != 0) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	error = zfs_link_create(dzp, dirname, zp, tx, ZNEW);
	if (error != 0) {
		zfs_znode_delete(zp, tx);
		zfs_znode_free(zp);
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_commit(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	zfs_acl_ids_free(&acl_ids);
	dmu_tx_commit(tx);

	*zpp = zp;
	zfs_exit(zfsvfs, FTAG);
	return (0);
}

int
zfs_rmdir(znode_t *dzp, const char *name, znode_t *cwd,
    cred_t *cr, int flags)
{
	(void) cwd; (void) cr; (void) flags;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	znode_t		*zp;
	dmu_tx_t	*tx;
	int		error;

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);

	error = zfs_dirent_lookup(dzp, name, &zp, ZEXISTS);
	if (error != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (ZTOTYPE(zp) != VDIR) {
		zfs_zrele(zp);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENOTDIR));
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
		zfs_zrele(zp);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	error = zfs_link_destroy(dzp, name, zp, tx, ZEXISTS, NULL);

	dmu_tx_commit(tx);
	zfs_zrele(zp);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

int
zfs_setattr(znode_t *zp, vattr_t *vap, int flag, cred_t *cr,
    zidmap_t *mnt_ns)
{
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	uint_t		mask = vap->va_mask;
	dmu_tx_t	*tx;
	int		error;
	sa_bulk_attr_t	bulk[3];
	int		count = 0;
	uint64_t	mode, ctime[2];
	timestruc_t	now;

	(void) flag; (void) cr; (void) mnt_ns;

	if (mask == 0)
		return (0);

	/* Only AT_MODE, AT_UID, AT_GID are handled; others succeed silently. */
	if (!(mask & (AT_MODE | AT_UID | AT_GID)))
		return (0);

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	mutex_enter(&zp->z_lock);

	if (mask & AT_MODE) {
		mode = (zp->z_mode & S_IFMT) | (vap->va_mode & ~S_IFMT);
		zp->z_mode = mode;
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs),
		    NULL, &mode, 8);
	}
	if (mask & AT_UID) {
		zp->z_uid = vap->va_uid;
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs),
		    NULL, &zp->z_uid, 8);
	}
	if (mask & AT_GID) {
		zp->z_gid = vap->va_gid;
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs),
		    NULL, &zp->z_gid, 8);
	}

	if (count > 0)
		error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);

	mutex_exit(&zp->z_lock);

	/* Update ctime on successful attribute change. */
	if (error == 0) {
		gethrestime(&now);
		ZFS_TIME_ENCODE(&now, ctime);
		(void) sa_update(zp->z_sa_hdl, SA_ZPL_CTIME(zfsvfs),
		    ctime, sizeof (ctime), tx);
	}

	dmu_tx_commit(tx);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * Cycle-check: verify that tdzp is not under szp in the directory tree.
 * Prevents moving a directory into one of its own descendants.
 * Returns EINVAL if a cycle would be created, 0 otherwise.
 */
static int
zfs_rename_check(znode_t *szp, znode_t *sdzp, znode_t *tdzp)
{
	zfsvfs_t	*zfsvfs = tdzp->z_zfsvfs;
	znode_t		*zp, *zp1;
	uint64_t	parent;
	int		error = 0;

	if (tdzp == szp)
		return (SET_ERROR(EINVAL));
	if (tdzp == sdzp)
		return (0);
	if (tdzp->z_id == zfsvfs->z_root)
		return (0);

	zp = tdzp;
	for (;;) {
		ASSERT(!zp->z_unlinked);
		if ((error = sa_lookup(zp->z_sa_hdl,
		    SA_ZPL_PARENT(zfsvfs), &parent, sizeof (parent))) != 0)
			break;
		if (parent == szp->z_id) {
			error = SET_ERROR(EINVAL);
			break;
		}
		if (parent == zfsvfs->z_root)
			break;
		if (parent == sdzp->z_id)
			break;
		error = zfs_zget(zfsvfs, parent, &zp1);
		if (error != 0)
			break;
		if (zp != tdzp)
			zfs_zrele(zp);
		zp = zp1;
	}
	if (zp != tdzp)
		zfs_zrele(zp);
	return (error);
}

int
zfs_rename(znode_t *sdzp, const char *snm, znode_t *tdzp,
    const char *tnm, cred_t *cr, int flags, uint64_t rflags,
    vattr_t *wo_vap, zidmap_t *mnt_ns)
{
	(void) cr; (void) flags; (void) rflags; (void) wo_vap; (void) mnt_ns;
	zfsvfs_t	*zfsvfs = ZTOZSB(sdzp);
	znode_t		*szp = NULL, *tzp = NULL;
	dmu_tx_t	*tx;
	int		error;

	if (strlen(tnm) >= ZAP_MAXNAMELEN)
		return (SET_ERROR(ENAMETOOLONG));

	if ((error = zfs_enter_verify_zp(zfsvfs, sdzp, FTAG)) != 0)
		return (error);
	if ((error = zfs_verify_zp(tdzp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/* Look up source entry. */
	error = zfs_dirent_lookup(sdzp, snm, &szp, ZEXISTS);
	if (error != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/* Renaming "." or ".." is invalid. */
	if (snm[0] == '.' && (snm[1] == '\0' ||
	    (snm[1] == '.' && snm[2] == '\0'))) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	/* If source is a directory, check for cycles. */
	if (ZTOTYPE(szp) == VDIR) {
		if ((error = zfs_rename_check(szp, sdzp, tdzp)) != 0)
			goto out;
	}

	/* Look up target entry (may not exist). */
	error = zfs_dirent_lookup(tdzp, tnm, &tzp, 0);
	if (error != 0 && error != ENOENT) {
		goto out;
	}
	error = 0;

	/* Source and target must be the same type. */
	if (tzp != NULL) {
		if (ZTOTYPE(szp) == VDIR && ZTOTYPE(tzp) != VDIR) {
			error = SET_ERROR(ENOTDIR);
			goto out;
		}
		if (ZTOTYPE(szp) != VDIR && ZTOTYPE(tzp) == VDIR) {
			error = SET_ERROR(EISDIR);
			goto out;
		}
		/* Same object: nothing to do. */
		if (szp->z_id == tzp->z_id) {
			error = 0;
			goto out;
		}
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_sa(tx, sdzp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, sdzp->z_id, FALSE, snm);
	dmu_tx_hold_zap(tx, tdzp->z_id, TRUE, tnm);
	if (sdzp != tdzp) {
		dmu_tx_hold_sa(tx, tdzp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, tdzp);
	}
	if (tzp != NULL) {
		dmu_tx_hold_sa(tx, tzp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, tzp);
	}
	zfs_sa_upgrade_txholds(tx, szp);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
		goto out;
	}

	/* Remove target if it exists. */
	if (tzp != NULL) {
		error = zfs_link_destroy(tdzp, tnm, tzp, tx, 0, NULL);
		if (error != 0) {
			dmu_tx_commit(tx);
			goto out;
		}
	}

	/* Add target entry pointing to szp. */
	error = zfs_link_create(tdzp, tnm, szp, tx, ZRENAMING);
	if (error == 0) {
		szp->z_pflags |= ZFS_AV_MODIFIED;
		(void) sa_update(szp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
		    &szp->z_pflags, sizeof (uint64_t), tx);

		/* Remove source entry. */
		error = zfs_link_destroy(sdzp, snm, szp, tx, ZRENAMING, NULL);
		if (error != 0) {
			/* Undo the target create to keep consistency. */
			VERIFY0(zfs_link_destroy(tdzp, tnm, szp, tx,
			    ZRENAMING, NULL));
		}
	}

	dmu_tx_commit(tx);

out:
	if (tzp != NULL)
		zfs_zrele(tzp);
	zfs_zrele(szp);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

int
zfs_symlink(znode_t *dzp, const char *name, vattr_t *vap,
    const char *link, znode_t **zpp, cred_t *cr, int flags,
    zidmap_t *mnt_ns)
{
	(void) cr; (void) flags; (void) mnt_ns;
	znode_t		*zp = NULL;
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	uint64_t	len = strlen(link);
	int		error;
	zfs_acl_ids_t	acl_ids;

	ASSERT3S(vap->va_type, ==, VLNK);

	if (strlen(name) >= ZAP_MAXNAMELEN)
		return (SET_ERROR(ENAMETOOLONG));

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);

	if (len > MAXPATHLEN) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENAMETOOLONG));
	}

	if ((error = zfs_acl_ids_create(dzp, 0, vap, kcred, NULL,
	    &acl_ids, NULL)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/* Fail if entry already exists. */
	error = zfs_dirent_lookup(dzp, name, &zp, ZNEW);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, MAX(1, len));
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE + len);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	zfs_mknode(dzp, vap, tx, kcred, 0, &zp, &acl_ids);

	if (zp->z_is_sa)
		error = sa_update(zp->z_sa_hdl, SA_ZPL_SYMLINK(zfsvfs),
		    (void *)(uintptr_t)link, len, tx);
	else
		zfs_sa_symlink(zp, (char *)(uintptr_t)link, (int)len, tx);

	zp->z_size = len;
	(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
	    &zp->z_size, sizeof (zp->z_size), tx);

	error = zfs_link_create(dzp, name, zp, tx, ZNEW);
	if (error != 0) {
		zfs_znode_delete(zp, tx);
		zfs_znode_free(zp);
		zp = NULL;
	}

	zfs_acl_ids_free(&acl_ids);
	dmu_tx_commit(tx);

	if (zpp != NULL)
		*zpp = (error == 0) ? zp : NULL;

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

int
zfs_link(znode_t *tdzp, znode_t *sp,
    const char *name, cred_t *cr, int flags)
{
	(void) tdzp; (void) sp; (void) name; (void) cr; (void) flags;
	return (SET_ERROR(ENOTSUP));
}

/*
 * zfs_space -- free/truncate a byte range of a file (F_FREESP).  Used by ZIL
 * replay (zfs_replay_truncate for TX_TRUNCATE records) and by ftruncate.  Left
 * as an ENOTSUP stub during bring-up, which made ZIL replay of a truncate
 * record fail ("ZFS replay transaction error 95 ... txtype 10") and abort the
 * rest of the log.  Implement it via the already-present zfs_freesp() (same
 * shape as the FreeBSD zfs_space): validate F_FREESP, refuse read-only, then
 * delegate to zfs_freesp(off, len).
 */
int
zfs_space(znode_t *zp, int cmd, struct flock *bfp, int flag,
    offset_t offset, cred_t *cr)
{
	(void) offset;
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	uint64_t	off, len;
	int		error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if (cmd != F_FREESP) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	if (zfs_is_readonly(zfsvfs)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EROFS));
	}

	if (bfp->l_len < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	if ((error = zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, cr, NULL))) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	off = bfp->l_start;
	len = bfp->l_len; /* 0 means from off to end of file */

	error = zfs_freesp(zp, off, len, flag, TRUE);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/* zfs_setsecattr is defined in common zfs_vnops.c */

/*
 * zfs_write_simple -- write @len bytes of @data at @pos into znode @zp,
 * synchronously, from kernel space.  Used by ZIL replay (zfs_replay_write for
 * TX_WRITE records) to re-apply logged writes on pool import after a crash.
 *
 * Left as an ENOTSUP stub during early bring-up, so a crash with sync=standard
 * (ZIL active on the SLOG) logged a "ZFS replay transaction error 95" and
 * dropped every logged write -- the ZIL provided no durability.  Implement it
 * the OSv way: build a one-iovec struct uio over the caller's buffer, wrap it
 * in a zfs_uio_t, and call the platform-independent zfs_write() with O_SYNC
 * (matching FreeBSD's vn_rdwr(UIO_SYSSPACE, IO_SYNC) shape).  zfsvfs->z_replay_eof
 * is set by the caller (zfs_replay_write) so a whole-block dmu_sync replay
 * write reduces the EOF correctly.
 */
int
zfs_write_simple(znode_t *zp, const void *data, size_t len,
    loff_t pos, size_t *resid)
{
	struct iovec iov;
	struct uio uio_s;
	zfs_uio_t zuio;
	int error;

	iov.iov_base = (void *)(uintptr_t)data;
	iov.iov_len = len;

	uio_s.uio_iov = &iov;
	uio_s.uio_iovcnt = 1;
	uio_s.uio_offset = (off_t)pos;
	uio_s.uio_resid = (ssize_t)len;
	uio_s.uio_rw = UIO_WRITE;

	zfs_uio_init(&zuio, &uio_s);
	error = zfs_write(zp, &zuio, O_SYNC, NULL);

	if (resid != NULL)
		*resid = (size_t)uio_s.uio_resid;

	/* Keep the OSv vnode's cached size coherent with the ZFS logical size,
	 * exactly as zfs_vop_write does after a file-extending write. */
	if (error == 0) {
		vnode_t *vp = ZTOV(zp);
		if (vp != NULL && vp->v_type == VREG)
			vp->v_size = (off_t)zp->z_size;
	}
	return (error);
}

/* ------------------------------------------------------------------ */
/* OSv VOP bridge functions                                            */
/* ------------------------------------------------------------------ */

/*
 * zfs_vop_open - OSv vop_open bridge.
 *
 * Unlike the old BSD code, we do NOT return EINVAL for O_DIRECT.
 * O_DIRECT is handled in the read/write paths via the ABD direct-I/O
 * mechanism.  We only reject it if the dataset has direct=disabled.
 */
static int
zfs_vop_open(struct file *fp)
{
	struct vnode *vp = file_dentry(fp)->d_vnode;
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if ((file_flags(fp) & FWRITE) && (zp->z_pflags & ZFS_APPENDONLY) &&
	    ((file_flags(fp) & O_APPEND) == 0)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	if (file_flags(fp) & O_DSYNC)
		atomic_inc_32(&zp->z_sync_cnt);

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * zfs_vop_close - OSv vop_close bridge.
 */
static int
zfs_vop_close(struct vnode *vp, struct file *fp)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if (file_flags(fp) & O_DSYNC)
		atomic_dec_32(&zp->z_sync_cnt);

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * zfs_vop_read - OSv vop_read bridge.
 *
 * Wraps the platform-independent zfs_read() with an OSv zfs_uio_t.
 * If O_DIRECT is set on the file, the ioflag is set accordingly so
 * zfs_setup_direct() will engage the ABD direct-I/O path.
 */
static int
zfs_vop_read(struct vnode *vp, struct file *fp, struct uio *uio, int flags)
{
	(void) flags;
	znode_t *zp = VTOZ(vp);
	zfs_uio_t zuio;
	int ioflag = 0;
	int error;

	/* Return EISDIR for directory reads, matching Linux behavior. */
	if (vp->v_type == VDIR)
		return (SET_ERROR(EISDIR));

	if (file_flags(fp) & O_DIRECT)
		ioflag |= O_DIRECT;
	if (file_flags(fp) & O_DSYNC)
		ioflag |= O_SYNC;

	zfs_uio_init(&zuio, uio);
	error = zfs_read(zp, &zuio, ioflag, NULL);

	if (zuio.uio_extflg & UIO_DIRECT)
		zfs_uio_free_dio_pages(&zuio, UIO_READ);

	return (error);
}

/*
 * zfs_vop_write - OSv vop_write bridge.
 *
 * The flags parameter from vfs_file::write() carries IO_APPEND,
 * IO_SYNC, and IO_DIRECT (the latter set when fp->f_flags & O_DIRECT).
 */
static int
zfs_vop_write(struct vnode *vp, struct uio *uio, int flags)
{
	znode_t *zp = VTOZ(vp);
	zfs_uio_t zuio;
	int ioflag = 0;
	int error;

	if (zfs_is_readonly(ZTOZSB(zp)))
		return (EROFS);

	if (flags & IO_APPEND)
		ioflag |= O_APPEND;
	if (flags & IO_SYNC)
		ioflag |= O_SYNC;
	if (flags & IO_DIRECT)
		ioflag |= O_DIRECT;

	zfs_uio_init(&zuio, uio);
	error = zfs_write(zp, &zuio, ioflag, NULL);

	if (zuio.uio_extflg & UIO_DIRECT)
		zfs_uio_free_dio_pages(&zuio, UIO_WRITE);

	/*
	 * zfs_write() updates the ZFS logical size (zp->z_size) but not the OSv
	 * vnode's cached size (vp->v_size), which zfs_vop_read/zfs_vop_cache use
	 * to bound reads.  Without this refresh a read of a region just written
	 * (file-extending write) is rejected as beyond-EOF.  Keep them in sync.
	 */
	if (error == 0 && vp->v_type == VREG)
		vp->v_size = (off_t)zp->z_size;

	return (error);
}

/*
 * zfs_vop_seek - trivially accept all seeks.
 */
static int
zfs_vop_seek(struct vnode *vp, struct file *fp, off_t ooff, off_t noffp)
{
	(void) vp; (void) fp; (void) ooff; (void) noffp;
	return (0);
}

/*
 * zfs_vop_ioctl - minimal ioctl support.
 */
static int
zfs_vop_ioctl(struct vnode *vp, struct file *fp, u_long com, void *data)
{
	(void) vp; (void) fp; (void) com; (void) data;
	return (SET_ERROR(ENOTTY));
}

/*
 * zfs_vop_fsync - flush pending writes to stable storage.
 */
static int
zfs_vop_fsync(struct vnode *vp, struct file *fp)
{
	(void) fp;
	return (zfs_fsync(VTOZ(vp), 0, NULL));
}

/*
 * zfs_vop_readdir - read one directory entry per call.
 *
 * fp->f_offset encodes position:
 *   0   → "." (current directory)
 *   1   → ".." (parent directory)
 *   >=2 → serialized ZAP cursor for real entries
 *
 * Returns 0 with dir filled on success; ENOENT when exhausted.
 */
static int
zfs_vop_readdir(struct vnode *vp, struct file *fp, struct dirent *dir)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	objset_t	*os;
	zap_cursor_t	zc;
	zap_attribute_t	*zap;
	uint64_t	offset;
	uint64_t	parent;
	uint64_t	objnum;
	uint8_t		dtype;
	int		error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent))) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	os = zfsvfs->z_os;
	offset = (uint64_t)file_offset(fp);
	zap = zap_attribute_long_alloc();

	if (offset == 0) {
		/* "." */
		strlcpy(dir->d_name, ".", sizeof (dir->d_name));
		objnum = zp->z_id;
		dtype = DT_DIR;
		file_setoffset(fp, 1);
	} else if (offset == 1) {
		/* ".." */
		strlcpy(dir->d_name, "..", sizeof (dir->d_name));
		objnum = parent;
		dtype = DT_DIR;
		file_setoffset(fp, 2);
	} else {
		/* Real ZAP entry */
		if (offset <= 2)
			zap_cursor_init(&zc, os, zp->z_id);
		else
			zap_cursor_init_serialized(&zc, os, zp->z_id, offset);

		error = zap_cursor_retrieve(&zc, zap);
		if (error != 0) {
			zap_cursor_fini(&zc);
			zap_attribute_free(zap);
			zfs_exit(zfsvfs, FTAG);
			return (error == ENOENT ? ENOENT : error);
		}

		if (zap->za_integer_length != 8 || zap->za_num_integers == 0) {
			zap_cursor_fini(&zc);
			zap_attribute_free(zap);
			zfs_exit(zfsvfs, FTAG);
			return (SET_ERROR(ENXIO));
		}

		objnum = ZFS_DIRENT_OBJ(zap->za_first_integer);
		dtype  = ZFS_DIRENT_TYPE(zap->za_first_integer);
		strlcpy(dir->d_name, zap->za_name, sizeof (dir->d_name));

		zap_cursor_advance(&zc);
		file_setoffset(fp, (off_t)zap_cursor_serialize(&zc));
		zap_cursor_fini(&zc);
	}

	dir->d_ino  = (ino_t)objnum;
	dir->d_off  = file_offset(fp);
	dir->d_type = dtype;
	dir->d_reclen = sizeof (struct dirent);

	zap_attribute_free(zap);
	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * zfs_vop_lookup - look up a directory entry and return its vnode.
 *
 * Uses ZAP to find the object ID for name in dvp, then calls vget()
 * to get/create the OSv vnode.  zfs_osv_vget() will load the znode
 * from disk if the vnode is not already cached.
 */
static int
zfs_vop_lookup(struct vnode *dvp, char *name, struct vnode **vpp)
{
	znode_t *dzp = VTOZ(dvp);
	zfsvfs_t *zfsvfs = ZTOZSB(dzp);
	uint64_t zoid;
	matchtype_t mt = 0;
	struct vnode *vp;
	int error;

	*vpp = NULL;

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);

	if (dvp->v_type != VDIR) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENOTDIR));
	}

	if (dzp->z_unlinked) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENOENT));
	}

	if (zfsvfs->z_norm != 0) {
		mt = MT_NORMALIZE;
		if (zfsvfs->z_case == ZFS_CASE_MIXED)
			mt |= MT_MATCH_CASE;
	}

	if (zfsvfs->z_norm) {
		error = zap_lookup_norm(zfsvfs->z_os, dzp->z_id, name, 8, 1,
		    &zoid, mt, NULL, 0, NULL);
	} else {
		error = zap_lookup(zfsvfs->z_os, dzp->z_id, name, 8, 1,
		    &zoid);
	}

	zfs_exit(zfsvfs, FTAG);

	if (error != 0)
		return (error);

	zoid = ZFS_DIRENT_OBJ(zoid);

	/*
	 * vget() looks up the vnode cache by (mount, inode).  If not
	 * cached, it allocates a new vnode and calls zfs_osv_vget() to
	 * load the znode from disk.
	 */
	vget(dvp->v_mount, zoid, &vp);
	if (vp == NULL)
		return (SET_ERROR(ENOMEM));

	*vpp = vp;
	return (0);
}

/*
 * zfs_vop_create - create a regular file.
 */
static int
zfs_vop_create(struct vnode *dvp, char *name, mode_t mode)
{
	znode_t		*dzp = VTOZ(dvp);
	znode_t		*zp = NULL;
	vattr_t		vattr;
	int		error;

	if (zfs_is_readonly(ZTOZSB(dzp)))
		return (EROFS);

	memset(&vattr, 0, sizeof (vattr));
	vattr.va_type = VREG;
	vattr.va_mode = mode;
	vattr.va_mask = AT_TYPE | AT_MODE;

	error = zfs_create(dzp, name, &vattr, 0, mode,
	    &zp, kcred, 0, NULL, NULL);
	if (error == 0 && zp != NULL)
		zfs_zrele(zp);
	return (error);
}

/*
 * zfs_vop_remove - remove a file (unlink).
 */
static int
zfs_vop_remove(struct vnode *dvp, struct vnode *vp, char *name)
{
	(void) vp;
	if (zfs_is_readonly(ZTOZSB(VTOZ(dvp))))
		return (EROFS);
	return (zfs_remove(VTOZ(dvp), name, kcred, 0));
}

/*
 * zfs_vop_rename - rename a file or directory.
 */
static int
zfs_vop_rename(struct vnode *sdvp, struct vnode *svp, char *sname,
    struct vnode *tdvp, struct vnode *tvp, char *tname)
{
	(void) svp; (void) tvp;
	if (zfs_is_readonly(ZTOZSB(VTOZ(sdvp))))
		return (EROFS);
	return (zfs_rename(VTOZ(sdvp), sname, VTOZ(tdvp), tname,
	    kcred, 0, 0, NULL, NULL));
}

/*
 * zfs_vop_mkdir - create a directory.
 */
static int
zfs_vop_mkdir(struct vnode *dvp, char *name, mode_t mode)
{
	znode_t		*dzp = VTOZ(dvp);
	znode_t		*zp = NULL;
	vattr_t		vattr;
	int		error;

	if (zfs_is_readonly(ZTOZSB(dzp)))
		return (EROFS);

	memset(&vattr, 0, sizeof (vattr));
	vattr.va_type = VDIR;
	vattr.va_mode = mode;
	vattr.va_mask = AT_TYPE | AT_MODE;

	error = zfs_mkdir(dzp, name, &vattr, &zp, kcred, 0, NULL, NULL);
	if (error == 0 && zp != NULL)
		zfs_zrele(zp);
	return (error);
}

/*
 * zfs_vop_rmdir - remove an empty directory.
 */
static int
zfs_vop_rmdir(struct vnode *dvp, struct vnode *vp, char *name)
{
	(void) vp;
	if (zfs_is_readonly(ZTOZSB(VTOZ(dvp))))
		return (EROFS);
	return (zfs_rmdir(VTOZ(dvp), name, NULL, kcred, 0));
}

/*
 * zfs_vop_getattr - return cached znode attributes.
 */
static int
zfs_vop_getattr(struct vnode *vp, struct vattr *vap)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;
	uint64_t mtime[2], ctime[2];
	sa_bulk_attr_t bulk[2];
	int count = 0;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);

	if ((error = sa_bulk_lookup(zp->z_sa_hdl, bulk, count)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	mutex_enter(&zp->z_lock);
	vap->va_type = IFTOVT(zp->z_mode);
	vap->va_mode = zp->z_mode & ~S_IFMT;
	vap->va_uid  = zp->z_uid;
	vap->va_gid  = zp->z_gid;
	vap->va_nodeid = zp->z_id;
	vap->va_nlink  = (nlink_t)MIN(zp->z_links, UINT32_MAX);
	vap->va_size   = zp->z_size;
	ZFS_TIME_DECODE(&vap->va_atime, zp->z_atime);
	ZFS_TIME_DECODE(&vap->va_mtime, mtime);
	ZFS_TIME_DECODE(&vap->va_ctime, ctime);
	mutex_exit(&zp->z_lock);

	/*
	 * Build st_dev from the filesystem ID stored at mount time.
	 * ZFS_ID is NOT encoded here; see zfs_domount() comment for rationale.
	 */
	{
		struct mount *mp = vp->v_mount;
		vap->va_fsid =
		    (dev_t)(uint32_t)mp->m_fsid.__val[0] |
		    ((dev_t)(uint32_t)mp->m_fsid.__val[1] << 32);
	}

	zfs_exit(zfsvfs, FTAG);
	return (0);
}

/*
 * zfs_vop_setattr - set vnode attributes (chmod, chown).
 */
static int
zfs_vop_setattr(struct vnode *vp, struct vattr *vap)
{
	znode_t *zp = VTOZ(vp);
	vattr_t zva;

	if (zfs_is_readonly(ZTOZSB(zp)))
		return (EROFS);

	memset(&zva, 0, sizeof (zva));
	zva.va_mask = 0;
	if (vap->va_mask & AT_MODE) {
		zva.va_mode = vap->va_mode;
		zva.va_mask |= AT_MODE;
	}
	if (vap->va_mask & AT_UID) {
		zva.va_uid = vap->va_uid;
		zva.va_mask |= AT_UID;
	}
	if (vap->va_mask & AT_GID) {
		zva.va_gid = vap->va_gid;
		zva.va_mask |= AT_GID;
	}
	if (zva.va_mask == 0)
		return (0);
	return (zfs_setattr(zp, &zva, 0, kcred, NULL));
}

/*
 * zfs_vop_inactive - release znode on last vnode reference drop.
 */
static int
zfs_vop_inactive(struct vnode *vp)
{
	znode_t *zp = VTOZ(vp);
	if (zp == NULL)
		return (0);

	vp->v_data = NULL;
	zp->z_vnode = NULL;

	/*
	 * zfs_zinactive destroys the SA handle under the object hold
	 * mutex and then frees the znode.  If the zfsvfs is already
	 * being torn down (z_sa_hdl is NULL), fall back to a direct
	 * free.
	 */
	if (zp->z_sa_hdl != NULL)
		zfs_zinactive(zp);
	else
		zfs_znode_free(zp);

	return (0);
}

/*
 * zfs_vop_truncate - truncate or extend a file to new_size.
 */
static int
zfs_vop_truncate(struct vnode *vp, off_t new_size)
{
	znode_t  *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int       error;

	if (zfs_is_readonly(zfsvfs))
		return (EROFS);

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);
	error = zfs_freesp(zp, (uint64_t)new_size, 0, O_RDWR, B_TRUE);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * zfs_vop_link - not yet implemented.
 */
static int
zfs_vop_link(struct vnode *tdvp, struct vnode *svp, char *name)
{
	(void) tdvp; (void) svp; (void) name;
	return (SET_ERROR(ENOTSUP));
}

/*
 * C-linkage helpers from core/pagecache.cc.
 * Used below by zfs_vop_cache() to register pages in OSv's read_cache.
 */
/* libsolaris.so is compiled with -fvisibility=hidden.  Without "default"
 * visibility the dynamic linker cannot resolve these symbols from loader.elf. */
extern __attribute__((visibility("default"))) void osv_pagecache_map_page(void *key, void *page);
extern __attribute__((visibility("default"))) void *osv_alloc_page(void);
extern __attribute__((visibility("default"))) void osv_free_page(void *p);
extern __attribute__((visibility("default"))) void osv_pagecache_map_arc_page(void *key, void *db, void *page);
extern __attribute__((visibility("default"))) void osv_pagecache_register_arc_rele(void (*rele)(void *db));

/* Tag identifying dbuf holds taken by the mmap ARC page-sharing bridge. */
static const char arc_page_tag[] = "osv_mmap_arc";

/*
 * osv_arc_dbuf_rele - release a dbuf hold taken by zfs_vop_cache()'s share
 * path.  Registered with the page cache (osv_pagecache_register_arc_rele) so
 * the cached_page_arc destructor can drop the hold when the page is evicted or
 * unmapped.  Runs in loader.elf context, so it must be C-linkage + default
 * visibility.
 */
__attribute__((visibility("default"))) void
osv_arc_dbuf_rele(void *db)
{
	dmu_buf_rele((dmu_buf_t *)db, arc_page_tag);
}

/*
 * zfs_vop_cache - populate one page of a ZFS file into OSv's pagecache.
 *
 * Called by vfs_file::read_page_from_cache() when a MAP_SHARED or MAP_PRIVATE
 * read fault misses the pagecache.  The uio carries:
 *   uio_iov->iov_base  - pointer to the pagecache::hashkey for this page
 *   uio_offset         - byte offset of the page (page-aligned)
 *   uio_resid          - mmu::page_size (4096)
 *
 * We read the page via zfs_read() (which is ARC-backed) into a newly
 * allocated physical page, register it with the pagecache via
 * osv_pagecache_map_page(), then set uio_resid = 0 to signal success.
 *
 * Multiple file_vma objects for the same file+offset share the same physical
 * page because pagecache::get() checks read_cache first and returns the
 * existing cached_page when found - this is what makes MAP_SHARED work.
 *
 * Note: ZFS_ID is NOT set in m_fsid so IS_ZFS() returns false.  This routes
 * ZFS files through the regular read_cache path (not the ARC bridge path),
 * avoiding arc_share_buf() which is a static-internal function in OpenZFS 2.x.
 */
static int
zfs_vop_cache(struct vnode *vp, struct file *fp, struct uio *uio)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	zfs_uio_t zuio;
	void *page;
	struct iovec iov;
	struct uio read_uio;
	int error;

	if (vp->v_type != VREG)
		return (EINVAL);
	if (uio->uio_offset < 0 || uio->uio_offset >= (off_t)vp->v_size)
		return (0);
	if (uio->uio_resid != PAGE_SIZE || uio->uio_offset % PAGE_SIZE)
		return (EINVAL);

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	/*
	 * Fast path: borrow the decompressed ARC page directly instead of
	 * copying it.  Pin the dbuf covering this offset; if the record is a
	 * whole number of pages, its db_data is page-aligned (see zio_init in
	 * module/zfs/zio.c) and the target page is fully backed, we can hand
	 * db_data + intra-record offset straight to the page cache with no
	 * memcpy.  The dbuf hold keeps that page resident; ownership of the
	 * hold transfers to the cached_page_arc, whose destructor releases it.
	 */
	dmu_buf_t *db = NULL;
	dmu_object_info_t doi;
	if (dmu_object_info(zfsvfs->z_os, zp->z_id, &doi) == 0 &&
	    (doi.doi_data_block_size % PAGE_SIZE) == 0 &&
	    dmu_buf_hold(zfsvfs->z_os, zp->z_id, uio->uio_offset,
	    arc_page_tag, &db, DMU_READ_PREFETCH) == 0) {
		if (IS_P2ALIGNED((uintptr_t)db->db_data, PAGE_SIZE) &&
		    uio->uio_offset + PAGE_SIZE <=
		    (off_t)(db->db_offset + db->db_size)) {
			void *shared = (char *)db->db_data +
			    (uio->uio_offset - db->db_offset);
			zfs_exit(zfsvfs, FTAG);
			/* Transfers the hold to the page cache; no rele here. */
			osv_pagecache_map_arc_page(uio->uio_iov->iov_base,
			    db, shared);
			uio->uio_resid = 0;
			return (0);
		}
		/* Not shareable (sub-page record or file tail): drop the hold. */
		dmu_buf_rele(db, arc_page_tag);
	}

	/* Slow path: copy the page out of the ARC into a fresh page. */
	page = osv_alloc_page();
	if (!page) {
		zfs_exit(zfsvfs, FTAG);
		return (ENOMEM);
	}
	memset(page, 0, PAGE_SIZE);

	iov.iov_base = page;
	iov.iov_len  = PAGE_SIZE;
	read_uio.uio_iov    = &iov;
	read_uio.uio_iovcnt = 1;
	read_uio.uio_offset = uio->uio_offset;
	read_uio.uio_resid  = PAGE_SIZE;
	read_uio.uio_rw     = UIO_READ;

	zfs_uio_init(&zuio, &read_uio);
	error = zfs_read(zp, &zuio, 0, kcred);
	zfs_exit(zfsvfs, FTAG);

	if (error) {
		osv_free_page(page);
		return (error);
	}

	osv_pagecache_map_page(uio->uio_iov->iov_base, page);
	uio->uio_resid = 0;
	return (0);
}

/*
 * zfs_vop_fallocate - not yet implemented.
 */
static int
zfs_vop_fallocate(struct vnode *vp, int mode, loff_t offset, loff_t len)
{
	(void) vp; (void) mode; (void) offset; (void) len;
	return (SET_ERROR(ENOTSUP));
}

/*
 * zfs_vop_readlink - read the target path of a symbolic link.
 */
static int
zfs_vop_readlink(struct vnode *vp, struct uio *uio)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	zfs_uio_t zuio;
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	zfs_uio_init(&zuio, uio);
	if (zp->z_is_sa)
		error = sa_lookup_uio(zp->z_sa_hdl,
		    SA_ZPL_SYMLINK(zfsvfs), &zuio);
	else
		error = zfs_sa_readlink(zp, &zuio);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * zfs_vop_symlink - create a symbolic link.
 */
static int
zfs_vop_symlink(struct vnode *dvp, char *name, char *link)
{
	znode_t *dzp = VTOZ(dvp);
	znode_t *zp = NULL;
	vattr_t vattr;
	int error;

	if (zfs_is_readonly(ZTOZSB(dzp)))
		return (EROFS);

	memset(&vattr, 0, sizeof (vattr));
	vattr.va_type = VLNK;
	vattr.va_mode = 0777;
	vattr.va_mask = AT_TYPE | AT_MODE;

	error = zfs_symlink(dzp, name, &vattr, link, &zp, kcred, 0, NULL);
	if (error == 0 && zp != NULL)
		zfs_zrele(zp);
	return (error);
}

/* ------------------------------------------------------------------ */
/* OSv vnode operations dispatch table                                 */
/* ------------------------------------------------------------------ */

struct vnops zfs_vnops = {
	zfs_vop_open,		/* open   */
	zfs_vop_close,		/* close  */
	zfs_vop_read,		/* read   */
	zfs_vop_write,		/* write  */
	zfs_vop_seek,		/* seek   */
	zfs_vop_ioctl,		/* ioctl  */
	zfs_vop_fsync,		/* fsync  */
	zfs_vop_readdir,	/* readdir */
	zfs_vop_lookup,		/* lookup */
	zfs_vop_create,		/* create */
	zfs_vop_remove,		/* remove */
	zfs_vop_rename,		/* rename */
	zfs_vop_mkdir,		/* mkdir  */
	zfs_vop_rmdir,		/* rmdir  */
	zfs_vop_getattr,	/* getattr */
	zfs_vop_setattr,	/* setattr */
	zfs_vop_inactive,	/* inactive */
	zfs_vop_truncate,	/* truncate */
	zfs_vop_link,		/* link   */
	zfs_vop_cache,		/* cache  */
	zfs_vop_fallocate,	/* fallocate */
	zfs_vop_readlink,	/* readlink */
	zfs_vop_symlink,	/* symlink */
};

#pragma GCC visibility pop
