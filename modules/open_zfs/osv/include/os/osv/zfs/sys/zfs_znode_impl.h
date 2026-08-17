// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * OSv znode implementation details for OpenZFS 2.3.6.
 *
 * OSv uses a simpler vnode model than FreeBSD. We maintain a z_vnode
 * pointer but manage reference counting manually via z_ref_cnt
 * since OSv's vnode layer doesn't provide the same refcounting
 * guarantees as FreeBSD.
 */

#ifndef	_OSV_ZFS_SYS_ZNODE_IMPL_H
#define	_OSV_ZFS_SYS_ZNODE_IMPL_H

#include <sys/list.h>
#include <sys/dmu.h>
#include <sys/sa.h>
#include <sys/zfs_vfsops.h>
#include <sys/rrwlock.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_stat.h>
#include <sys/zfs_rlock.h>
#include <sys/zil.h>
#include <sys/zfs_project.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * OS-specific znode fields.
 *
 * OSv does not have FreeBSD's vnode refcounting (vhold/vrele), so we
 * use a manual reference count z_ref_cnt. The z_vnode pointer links
 * to OSv's lightweight vnode structure.
 */
#define	ZNODE_OS_FIELDS				\
	struct zfsvfs	*z_zfsvfs;		\
	struct vnode	*z_vnode;		\
	uint64_t	z_uid;			\
	uint64_t	z_gid;			\
	uint64_t	z_gen;			\
	uint64_t	z_atime[2];		\
	uint64_t	z_links;		\
	uint32_t	z_ref_cnt;

#define	ZFS_LINK_MAX	UINT64_MAX

/*
 * Convert between znode pointers and vnode pointers.
 */
#define	ZTOV(ZP)	((ZP)->z_vnode)
#define	ZTOI(ZP)	((ZP)->z_vnode)
#define	VTOZ(VP)	((struct znode *)(VP)->v_data)
#define	VTOZ_SMR(VP)	VTOZ(VP)
#define	ITOZ(VP)	VTOZ(VP)

/*
 * OSv znode reference counting (manual).
 */
extern void zfs_zhold(struct znode *zp);
extern void zfs_zrele(struct znode *zp);
#define	zhold(zp)	zfs_zhold(zp)
#define	zrele(zp)	zfs_zrele(zp)

#define	ZTOZSB(zp)	((zp)->z_zfsvfs)
#define	ITOZSB(vp)	(VTOZ(vp)->z_zfsvfs)
#define	ZTOTYPE(zp)	(ZTOV(zp)->v_type)
#define	ZTOGID(zp)	((zp)->z_gid)
#define	ZTOUID(zp)	((zp)->z_uid)
#define	ZTONLNK(zp)	((zp)->z_links)
#define	Z_ISBLK(type)	((type) == VBLK)
#define	Z_ISCHR(type)	((type) == VCHR)
#define	Z_ISLNK(type)	((type) == VLNK)
#define	Z_ISDIR(type)	((type) == VDIR)

/* Cached data operations (no-op on OSv -- no page cache integration) */
#define	zn_has_cached_data(zp, start, end)	(0)
#define	zn_flush_cached_data(zp, sync)		do { } while (0)
#define	zn_rlimit_fsize(size)			(0)
#define	zn_rlimit_fsize_uio(zp, uio)		(0)

/* Called on entry to each ZFS vnode and vfs operation */
static inline int
zfs_enter(zfsvfs_t *zfsvfs, const char *tag)
{
	ZFS_TEARDOWN_ENTER_READ(zfsvfs, tag);
	if (__predict_false((zfsvfs)->z_unmounted)) {
		ZFS_TEARDOWN_EXIT_READ(zfsvfs, tag);
		return (SET_ERROR(EIO));
	}
	return (0);
}

/* Must be called before exiting the vop */
static inline void
zfs_exit(zfsvfs_t *zfsvfs, const char *tag)
{
	ZFS_TEARDOWN_EXIT_READ(zfsvfs, tag);
}

/*
 * Macros for dealing with dmu_buf_hold.
 */
#define	ZFS_OBJ_HASH(obj_num)	((obj_num) & (ZFS_OBJ_MTX_SZ - 1))
#define	ZFS_OBJ_MUTEX(zfsvfs, obj_num)	\
	(&(zfsvfs)->z_hold_mtx[ZFS_OBJ_HASH(obj_num)])
#define	ZFS_OBJ_HOLD_ENTER(zfsvfs, obj_num) \
	mutex_enter(ZFS_OBJ_MUTEX((zfsvfs), (obj_num)))
#define	ZFS_OBJ_HOLD_TRYENTER(zfsvfs, obj_num) \
	mutex_tryenter(ZFS_OBJ_MUTEX((zfsvfs), (obj_num)))
#define	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num) \
	mutex_exit(ZFS_OBJ_MUTEX((zfsvfs), (obj_num)))

/* Encode ZFS stored time values from a struct timespec */
#define	ZFS_TIME_ENCODE(tp, stmp)		\
{						\
	(stmp)[0] = (uint64_t)(tp)->tv_sec;	\
	(stmp)[1] = (uint64_t)(tp)->tv_nsec;	\
}

/* Decode ZFS stored time values to a struct timespec */
#define	ZFS_TIME_DECODE(tp, stmp)		\
{						\
	(tp)->tv_sec = (time_t)(stmp)[0];	\
	(tp)->tv_nsec = (long)(stmp)[1];	\
}

#define	ZFS_ACCESSTIME_STAMP(zfsvfs, zp)

extern void zfs_tstamp_update_setup_ext(struct znode *,
    uint_t, uint64_t [2], uint64_t [2], boolean_t have_tx);
extern void zfs_znode_free(struct znode *);
extern void zfs_znode_dmu_fini(struct znode *);
extern void zfs_znode_sa_init(struct zfsvfs *, struct znode *,
    struct dmu_buf *, dmu_object_type_t, sa_handle_t *);
extern void zfs_mknode(struct znode *, vattr_t *, dmu_tx_t *, cred_t *,
    uint_t, struct znode **, struct zfs_acl_ids *);
extern void zfs_znode_delete(struct znode *, dmu_tx_t *);

extern zil_replay_func_t *const zfs_replay_vector[TX_MAX_TYPE];

extern int zfs_znode_parent_and_name(struct znode *zp, struct znode **dzpp,
    char *buf, uint64_t buflen);

#ifdef	__cplusplus
}
#endif

#endif	/* _OSV_ZFS_SYS_ZNODE_IMPL_H */
