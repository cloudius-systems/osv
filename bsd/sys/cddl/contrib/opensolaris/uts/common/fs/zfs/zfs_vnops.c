/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright 2014 Nexenta Systems, Inc.  All rights reserved.
 */

/* Portions Copyright 2007 Jeremy Teo */
/* Portions Copyright 2010 Robert Milkowski */

/*
 * Copyright (c) 2011, Lawrence Livermore National Security, LLC.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/resource.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/taskq.h>
#include <sys/uio.h>
#include <sys/atomic.h>
#include <sys/namei.h>
#include <sys/mman.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/dirent.h>
#include <sys/policy.h>
#include <sys/sunddi.h>
#include <sys/sid.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/zfs_sa.h>
#include <sys/dnlc.h>
#include <sys/zfs_rlock.h>
#include <sys/extdirent.h>
#include <sys/kidmap.h>
#include <sys/acl.h>
#include <sys/fcntl.h>

/*
 * Programming rules.
 *
 * Each vnode op performs some logical unit of work.  To do this, the ZPL must
 * properly lock its in-core state, create a DMU transaction, do the work,
 * record this work in the intent log (ZIL), commit the DMU transaction,
 * and wait for the intent log to commit if it is a synchronous operation.
 * Moreover, the vnode ops must work in both normal and log replay context.
 * The ordering of events is important to avoid deadlocks and references
 * to freed memory.  The example below illustrates the following Big Rules:
 *
 *  (1) A check must be made in each zfs thread for a mounted file system.
 *	This is done avoiding races using ZFS_ENTER(zfsvfs).
 *      A ZFS_EXIT(zfsvfs) is needed before all returns.  Any znodes
 *      must be checked with ZFS_VERIFY_ZP(zp).  Both of these macros
 *      can return EIO from the calling function.
 *
 *  (2)	VN_RELE() should always be the last thing except for zil_commit()
 *	(if necessary) and ZFS_EXIT(). This is for 3 reasons:
 *	First, if it's the last reference, the vnode/znode
 *	can be freed, so the zp may point to freed memory.  Second, the last
 *	reference will call zfs_zinactive(), which may induce a lot of work --
 *	pushing cached pages (which acquires range locks) and syncing out
 *	cached atime changes.  Third, zfs_zinactive() may require a new tx,
 *	which could deadlock the system if you were already holding one.
 *	If you must call VN_RELE() within a tx then use VN_RELE_ASYNC().
 *
 *  (3)	All range locks must be grabbed before calling dmu_tx_assign(),
 *	as they can span dmu_tx_assign() calls.
 *
 *  (4)	Always pass TXG_NOWAIT as the second argument to dmu_tx_assign().
 *	This is critical because we don't want to block while holding locks.
 *	Note, in particular, that if a lock is sometimes acquired before
 *	the tx assigns, and sometimes after (e.g. z_lock), then failing to
 *	use a non-blocking assign can deadlock the system.  The scenario:
 *
 *	Thread A has grabbed a lock before calling dmu_tx_assign().
 *	Thread B is in an already-assigned tx, and blocks for this lock.
 *	Thread A calls dmu_tx_assign(TXG_WAIT) and blocks in txg_wait_open()
 *	forever, because the previous txg can't quiesce until B's tx commits.
 *
 *	If dmu_tx_assign() returns ERESTART and zfsvfs->z_assign is TXG_NOWAIT,
 *	then drop all locks, call dmu_tx_wait(), and try again.
 *
 *  (5)	If the operation succeeded, generate the intent log entry for it
 *	before dropping locks.  This ensures that the ordering of events
 *	in the intent log matches the order in which they actually occurred.
 *      During ZIL replay the zfs_log_* functions will update the sequence
 *	number to indicate the zil transaction has replayed.
 *
 *  (6)	At the end of each vnode op, the DMU tx must always commit,
 *	regardless of whether there were any errors.
 *
 *  (7)	After dropping all locks, invoke zil_commit(zilog, foid)
 *	to ensure that synchronous semantics are provided when necessary.
 *
 * In general, this is how things should be ordered in each vnode op:
 *
 *	ZFS_ENTER(zfsvfs);		// exit if unmounted
 * top:
 *	zfs_dirent_lock(&dl, ...)	// lock directory entry (may VN_HOLD())
 *	rw_enter(...);			// grab any other locks you need
 *	tx = dmu_tx_create(...);	// get DMU tx
 *	dmu_tx_hold_*();		// hold each object you might modify
 *	error = dmu_tx_assign(tx, TXG_NOWAIT);	// try to assign
 *	if (error) {
 *		rw_exit(...);		// drop locks
 *		zfs_dirent_unlock(dl);	// unlock directory entry
 *		VN_RELE(...);		// release held vnodes
 *		if (error == ERESTART) {
 *			dmu_tx_wait(tx);
 *			dmu_tx_abort(tx);
 *			goto top;
 *		}
 *		dmu_tx_abort(tx);	// abort DMU tx
 *		ZFS_EXIT(zfsvfs);	// finished in zfs
 *		return (error);		// really out of space
 *	}
 *	error = do_real_work();		// do whatever this VOP does
 *	if (error == 0)
 *		zfs_log_*(...);		// on success, make ZIL entry
 *	dmu_tx_commit(tx);		// commit DMU tx -- error or not
 *	rw_exit(...);			// drop locks
 *	zfs_dirent_unlock(dl);		// unlock directory entry
 *	VN_RELE(...);			// release held vnodes
 *	zil_commit(zilog, foid);	// synchronous when necessary
 *	ZFS_EXIT(zfsvfs);		// finished in zfs
 *	return (error);			// done, report error
 */

static int
zfs_open(struct file *fp)
{
	struct vnode *vp = file_dentry(fp)->d_vnode;
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (file_flags(fp) & O_DIRECT) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	if ((file_flags(fp) & FWRITE) && (zp->z_pflags & ZFS_APPENDONLY) &&
	    ((file_flags(fp) & O_APPEND) == 0)) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	/* Keep a count of the synchronous opens in the znode */
	if (file_flags(fp) & O_DSYNC)
		atomic_inc_32(&zp->z_sync_cnt);

	ZFS_EXIT(zfsvfs);
	return (0);
}

/* ARGSUSED */
static int
zfs_close(vnode_t *vp, file_t *fp)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/* Decrement the synchronous opens in the znode */
	if (file_flags(fp) & O_DSYNC)
		atomic_dec_32(&zp->z_sync_cnt);

	ZFS_EXIT(zfsvfs);
	return (0);
}

/*
 * Lseek support for finding holes (cmd == _FIO_SEEK_HOLE) and
 * data (cmd == _FIO_SEEK_DATA). "off" is an in/out parameter.
 */
static int
zfs_holey(vnode_t *vp, u_long cmd, offset_t *off)
{
	znode_t	*zp = VTOZ(vp);
	uint64_t noff = (uint64_t)*off; /* new offset */
	uint64_t file_sz;
	int error;
	boolean_t hole;

	file_sz = zp->z_size;
	if (noff >= file_sz)  {
		return (ENXIO);
	}

	if (cmd == _FIO_SEEK_HOLE)
		hole = B_TRUE;
	else
		hole = B_FALSE;

	error = dmu_offset_next(zp->z_zfsvfs->z_os, zp->z_id, hole, &noff);

	/* end of file? */
	if ((error == ESRCH) || (noff > file_sz)) {
		/*
		 * Handle the virtual hole at the end of file.
		 */
		if (hole) {
			*off = file_sz;
			return (0);
		}
		return (ENXIO);
	}

	if (noff < *off)
		return (error);
	*off = noff;
	return (error);
}

/* ARGSUSED */
static int
zfs_ioctl(vnode_t *vp, file_t *fp, u_long com, void *data)
{
	offset_t off;
	int error;
	zfsvfs_t *zfsvfs;
	znode_t *zp;
	int flag = 0;
	cred_t *cr = CRED();

	switch (com) {
	case _FIOFFS:
		return (0);

		/*
		 * The following two ioctls are used by bfu.  Faking out,
		 * necessary to avoid bfu errors.
		 */
	case _FIOGDIO:
	case _FIOSDIO:
		return (0);

	case _FIO_SEEK_DATA:
	case _FIO_SEEK_HOLE:
#ifdef sun
		if (ddi_copyin((void *)data, &off, sizeof (off), flag))
			return (SET_ERROR(EFAULT));
#else
		off = *(offset_t *)data;
#endif
		zp = VTOZ(vp);
		zfsvfs = zp->z_zfsvfs;
		ZFS_ENTER(zfsvfs);
		ZFS_VERIFY_ZP(zp);

		/* offset parameter is in/out */
		error = zfs_holey(vp, com, &off);
		ZFS_EXIT(zfsvfs);
		if (error)
			return (error);
#ifdef sun
		if (ddi_copyout(&off, (void *)data, sizeof (off), flag))
			return (SET_ERROR(EFAULT));
#else
		*(offset_t *)data = off;
#endif
		return (0);
	}
	return (ENOTTY);
}

#ifdef NOTYET
static vm_page_t
page_lookup(vnode_t *vp, int64_t start, int64_t off, int64_t nbytes)
{
	vm_object_t obj;
	vm_page_t pp;

	obj = vp->v_object;
	VM_OBJECT_LOCK_ASSERT(obj, MA_OWNED);

	for (;;) {
		if ((pp = vm_page_lookup(obj, OFF_TO_IDX(start))) != NULL &&
		    vm_page_is_valid(pp, (vm_offset_t)off, nbytes)) {
			if ((pp->oflags & VPO_BUSY) != 0) {
				/*
				 * Reference the page before unlocking and
				 * sleeping so that the page daemon is less
				 * likely to reclaim it.
				 */
				vm_page_reference(pp);
				vm_page_sleep(pp, "zfsmwb");
				continue;
			}
			vm_page_busy(pp);
			vm_page_undirty(pp);
		} else {
			if (__predict_false(obj->cache != NULL)) {
				vm_page_cache_free(obj, OFF_TO_IDX(start),
				    OFF_TO_IDX(start) + 1);
			}
			pp = NULL;
		}
		break;
	}
	return (pp);
}

static void
page_unlock(vm_page_t pp)
{

	vm_page_wakeup(pp);
}

static caddr_t
zfs_map_page(vm_page_t pp, struct sf_buf **sfp)
{

	*sfp = sf_buf_alloc(pp, 0);
	return ((caddr_t)sf_buf_kva(*sfp));
}

static void
zfs_unmap_page(struct sf_buf *sf)
{

	sf_buf_free(sf);
}

/*
 * When a file is memory mapped, we must keep the IO data synchronized
 * between the DMU cache and the memory mapped pages.  What this means:
 *
 * On Write:	If we find a memory mapped page, we write to *both*
 *		the page and the dmu buffer.
 */
static void
update_pages(vnode_t *vp, int64_t start, int len, objset_t *os, uint64_t oid,
    int segflg, dmu_tx_t *tx)
{
	vm_object_t obj;
	struct sf_buf *sf;
	int off;

	ASSERT(vp->v_mount != NULL);
	obj = vp->v_object;
	ASSERT(obj != NULL);

	off = start & PAGEOFFSET;
	VM_OBJECT_LOCK(obj);
	for (start &= PAGEMASK; len > 0; start += PAGESIZE) {
		vm_page_t pp;
		int nbytes = MIN(PAGESIZE - off, len);

		if ((pp = page_lookup(vp, start, off, nbytes)) != NULL) {
			caddr_t va;

			VM_OBJECT_UNLOCK(obj);
			va = zfs_map_page(pp, &sf);
			if (segflg == UIO_NOCOPY) {
				(void) dmu_write(os, oid, start+off, nbytes,
				    va+off, tx);
			} else {
				(void) dmu_read(os, oid, start+off, nbytes,
				    va+off, DMU_READ_PREFETCH);
			}
			zfs_unmap_page(sf);
			VM_OBJECT_LOCK(obj);
			page_unlock(pp);
		}
		len -= nbytes;
		off = 0;
	}
	VM_OBJECT_UNLOCK(obj);
}

/*
 * Read with UIO_NOCOPY flag means that sendfile(2) requests
 * ZFS to populate a range of page cache pages with data.
 *
 * NOTE: this function could be optimized to pre-allocate
 * all pages in advance, drain VPO_BUSY on all of them,
 * map them into contiguous KVA region and populate them
 * in one single dmu_read() call.
 */
static int
mappedread_sf(vnode_t *vp, int nbytes, uio_t *uio)
{
	znode_t *zp = VTOZ(vp);
	objset_t *os = zp->z_zfsvfs->z_os;
	struct sf_buf *sf;
	vm_object_t obj;
	vm_page_t pp;
	int64_t start;
	caddr_t va;
	int len = nbytes;
	int off;
	int error = 0;

	ASSERT(uio->uio_segflg == UIO_NOCOPY);
	ASSERT(vp->v_mount != NULL);
	obj = vp->v_object;
	ASSERT(obj != NULL);
	ASSERT((uio->uio_loffset & PAGEOFFSET) == 0);

	VM_OBJECT_LOCK(obj);
	for (start = uio->uio_loffset; len > 0; start += PAGESIZE) {
		int bytes = MIN(PAGESIZE, len);

		pp = vm_page_grab(obj, OFF_TO_IDX(start), VM_ALLOC_NOBUSY |
		    VM_ALLOC_NORMAL | VM_ALLOC_RETRY | VM_ALLOC_IGN_SBUSY);
		if (pp->valid == 0) {
			vm_page_io_start(pp);
			VM_OBJECT_UNLOCK(obj);
			va = zfs_map_page(pp, &sf);
			error = dmu_read(os, zp->z_id, start, bytes, va,
			    DMU_READ_PREFETCH);
			if (bytes != PAGESIZE && error == 0)
				bzero(va + bytes, PAGESIZE - bytes);
			zfs_unmap_page(sf);
			VM_OBJECT_LOCK(obj);
			vm_page_io_finish(pp);
			vm_page_lock(pp);
			if (error) {
				vm_page_free(pp);
			} else {
				pp->valid = VM_PAGE_BITS_ALL;
				vm_page_activate(pp);
			}
			vm_page_unlock(pp);
		}
		if (error)
			break;
		uio->uio_resid -= bytes;
		uio->uio_offset += bytes;
		len -= bytes;
	}
	VM_OBJECT_UNLOCK(obj);
	return (error);
}

/*
 * When a file is memory mapped, we must keep the IO data synchronized
 * between the DMU cache and the memory mapped pages.  What this means:
 *
 * On Read:	We "read" preferentially from memory mapped pages,
 *		else we default from the dmu buffer.
 *
 * NOTE: We will always "break up" the IO into PAGESIZE uiomoves when
 *	the file is memory mapped.
 */
static int
mappedread(vnode_t *vp, int nbytes, uio_t *uio)
{
	znode_t *zp = VTOZ(vp);
	objset_t *os = zp->z_zfsvfs->z_os;
	vm_object_t obj;
	int64_t start;
	caddr_t va;
	int len = nbytes;
	int off;
	int error = 0;

	ASSERT(vp->v_mount != NULL);
	obj = vp->v_object;
	ASSERT(obj != NULL);

	start = uio->uio_loffset;
	off = start & PAGEOFFSET;
	VM_OBJECT_LOCK(obj);
	for (start &= PAGEMASK; len > 0; start += PAGESIZE) {
		vm_page_t pp;
		uint64_t bytes = MIN(PAGESIZE - off, len);

		if (pp = page_lookup(vp, start, off, bytes)) {
			struct sf_buf *sf;
			caddr_t va;

			VM_OBJECT_UNLOCK(obj);
			va = zfs_map_page(pp, &sf);
			error = uiomove(va + off, bytes, UIO_READ, uio);
			zfs_unmap_page(sf);
			VM_OBJECT_LOCK(obj);
			page_unlock(pp);
		} else {
			VM_OBJECT_UNLOCK(obj);
			error = dmu_read_uio(os, zp->z_id, uio, bytes);
			VM_OBJECT_LOCK(obj);
		}
		len -= bytes;
		off = 0;
		if (error)
			break;
	}
	VM_OBJECT_UNLOCK(obj);
	return (error);
}
#endif /* NOTYET */

offset_t zfs_read_chunk_size = 1024 * 1024; /* Tunable */

/*
 * Read bytes from specified file into supplied buffer.
 *
 *	IN:	vp	- vnode of file to be read from.
 *		fp	- file to be read from.
 *		uio	- structure supplying read location, range info,
 *			  and return buffer.
 *		ioflag	- SYNC flags; used to provide FRSYNC semantics.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	uio	- updated offset and range, buffer filled.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Side Effects:
 *	vp - atime updated if byte count > 0
 */
/* ARGSUSED */
static int
zfs_read(vnode_t *vp, struct file* fp, uio_t *uio, int ioflag)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	objset_t	*os;
	ssize_t		n, nbytes;
	int		error;
	rl_t		*rl;
#ifdef sun
	xuio_t		*xuio = NULL;
#endif

	// Return EISDIR when reading from a directory, as Linux does.
	if (vp->v_type == VDIR) {
	    return EISDIR;
	}

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	os = zfsvfs->z_os;

	if (zp->z_pflags & ZFS_AV_QUARANTINED) {
		ZFS_EXIT(zfsvfs);
		return (EACCES);
	}

	/*
	 * Validate file offset
	 */
	if (uio->uio_loffset < (offset_t)0) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	/*
	 * Fasttrack empty reads
	 */
	if (uio->uio_resid == 0) {
		ZFS_EXIT(zfsvfs);
		return (0);
	}

#ifndef __OSV__
	/*
	 * Check for mandatory locks
	 */
	if (MANDMODE(zp->z_mode)) {
		if (error = chklock(vp, FREAD,
		    uio->uio_loffset, uio->uio_resid, uio->uio_fmode, ct)) {
			ZFS_EXIT(zfsvfs);
			return (error);
		}
	}

	/*
	 * If we're in FRSYNC mode, sync out this znode before reading it.
	 */
	if (zfsvfs->z_log &&
	    (ioflag & IO_FRSYNC || zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS))
		zil_commit(zfsvfs->z_log, zp->z_id);
#endif

	/*
	 * Lock the range against changes.
	 */
	rl = zfs_range_lock(zp, uio->uio_loffset, uio->uio_resid, RL_READER);

	/*
	 * If we are reading past end-of-file we can skip
	 * to the end; but we might still need to set atime.
	 */
	if (uio->uio_loffset >= zp->z_size) {
		error = 0;
		goto out;
	}

	ASSERT(uio->uio_loffset < zp->z_size);
	n = MIN(uio->uio_resid, zp->z_size - uio->uio_loffset);

#ifdef sun
	if ((uio->uio_extflg == UIO_XUIO) &&
	    (((xuio_t *)uio)->xu_type == UIOTYPE_ZEROCOPY)) {
		int nblk;
		int blksz = zp->z_blksz;
		uint64_t offset = uio->uio_loffset;

		xuio = (xuio_t *)uio;
		if ((ISP2(blksz))) {
			nblk = (P2ROUNDUP(offset + n, blksz) - P2ALIGN(offset,
			    blksz)) / blksz;
		} else {
			ASSERT(offset + n <= blksz);
			nblk = 1;
		}
		(void) dmu_xuio_init(xuio, nblk);

		if (vn_has_cached_data(vp)) {
			/*
			 * For simplicity, we always allocate a full buffer
			 * even if we only expect to read a portion of a block.
			 */
			while (--nblk >= 0) {
				(void) dmu_xuio_add(xuio,
				    dmu_request_arcbuf(sa_get_db(zp->z_sa_hdl),
				    blksz), 0, blksz);
			}
		}
	}
#endif	/* sun */

	while (n > 0) {
		nbytes = MIN(n, zfs_read_chunk_size -
		    P2PHASE(uio->uio_loffset, zfs_read_chunk_size));

#ifdef __FreeBSD__
		if (uio->uio_segflg == UIO_NOCOPY)
			error = mappedread_sf(vp, nbytes, uio);
		else
#endif /* __FreeBSD__ */
#ifndef __OSV__
		if (vn_has_cached_data(vp))
			error = mappedread(vp, nbytes, uio);
		else
#endif
			error = dmu_read_uio(os, zp->z_id, uio, nbytes);
		if (error) {
			/* convert checksum errors into IO errors */
			if (error == ECKSUM)
				error = EIO;
			break;
		}

		n -= nbytes;
	}
out:
	zfs_range_unlock(rl);

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);
	ZFS_EXIT(zfsvfs);
	return (error);
}


static int zfs_truncate(struct vnode *vp, off_t new_size);

static int
zfs_arc(vnode_t *vp, struct file* fp, uio_t *uio)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	objset_t	*os;
	ssize_t		nbytes = uio->uio_resid;
	int			error = EIO;
	rl_t		*rl;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	os = zfsvfs->z_os;

	if (zp->z_pflags & ZFS_AV_QUARANTINED) {
		ZFS_EXIT(zfsvfs);
		return (EACCES);
	}

	/*
	 * Fasttrack empty reads
	 */
	if (uio->uio_resid == 0) {
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	if ((uio->uio_loffset + uio->uio_resid) > zp->z_size) {
		nbytes -= (uio->uio_loffset + uio->uio_resid) - zp->z_size;
	}

	/*
	 * Validate file offset
	 */
	if (uio->uio_loffset < (offset_t)0) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	/*
	 * Lock the range against changes.
	 */
	rl = zfs_range_lock(zp, uio->uio_loffset, uio->uio_resid, RL_READER);

	nbytes = MIN(nbytes, zfs_read_chunk_size -
		P2PHASE(uio->uio_loffset, zfs_read_chunk_size));

	error = dmu_map_uio(os, zp->z_id, uio, nbytes);
	if (error) {
		/* convert checksum errors into IO errors */
		if (error == ECKSUM)
			error = EIO;
	}

	zfs_range_unlock(rl);

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);
	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Write the bytes to a file.
 *
 *	IN:	vp	- vnode of file to be written to.
 *		uio	- structure supplying write location, range info,
 *			  and data buffer.
 *		ioflag	- FAPPEND flag set if in append mode.
 *
 *	OUT:	uio	- updated offset and range.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - ctime|mtime updated if byte count > 0
 */

/* ARGSUSED */
static int
zfs_write(vnode_t *vp, uio_t *uio, int ioflag)
{
	znode_t		*zp = VTOZ(vp);
	rlim64_t        limit = MAXOFFSET_T;
	ssize_t		start_resid = uio->uio_resid;
	ssize_t		tx_bytes;
	uint64_t	end_size;
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	zilog_t		*zilog;
	offset_t	woff;
	ssize_t		n, nbytes;
	rl_t		*rl;
	int		max_blksz = zfsvfs->z_max_blksz;
	int		error;
	arc_buf_t	*abuf;
	int		iovcnt = uio->uio_iovcnt;
	iovec_t		*iovp = uio->uio_iov;
	int		write_eof;
	int		count = 0;
	sa_bulk_attr_t	bulk[4];
	uint64_t	mtime[2], ctime[2];

	/*
	 * Fasttrack empty write
	 */
	n = start_resid;
	if (n == 0)
		return (0);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &zp->z_size, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, 8);

	/*
	 * If immutable or not appending then return EPERM
	 */
	if ((zp->z_pflags & (ZFS_IMMUTABLE | ZFS_READONLY)) ||
	    ((zp->z_pflags & ZFS_APPENDONLY) && !(ioflag & IO_APPEND) &&
	    (uio->uio_loffset < zp->z_size))) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	zilog = zfsvfs->z_log;

	/*
	 * Validate file offset
	 */
	woff = ioflag & IO_APPEND ? zp->z_size : uio->uio_loffset;
	if (woff < 0) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	/*
	 * If in append mode, set the io offset pointer to eof.
	 */
	if (ioflag & IO_APPEND) {
		/*
		 * Obtain an appending range lock to guarantee file append
		 * semantics.  We reset the write offset once we have the lock.
		 */
		rl = zfs_range_lock(zp, 0, n, RL_APPEND);
		woff = rl->r_off;
		if (rl->r_len == UINT64_MAX) {
			/*
			 * We overlocked the file because this write will cause
			 * the file block size to increase.
			 * Note that zp_size cannot change with this lock held.
			 */
			woff = zp->z_size;
		}
		uio->uio_loffset = woff;
	} else {
		/*
		 * Note that if the file block size will change as a result of
		 * this write, then this range lock will lock the entire file
		 * so that we can re-write the block safely.
		 */
		rl = zfs_range_lock(zp, woff, n, RL_WRITER);
	}

	if (woff >= limit) {
		zfs_range_unlock(rl);
		ZFS_EXIT(zfsvfs);
		return (EFBIG);
	}

	if ((woff + n) > limit || woff > (limit - n))
		n = limit - woff;

	/* Will this write extend the file length? */
	write_eof = (woff + n > zp->z_size);

	end_size = MAX(zp->z_size, woff + n);

	/*
	 * Write the file in reasonable size chunks.  Each chunk is written
	 * in a separate transaction; this keeps the intent log records small
	 * and allows us to do more fine-grained space accounting.
	 */
	while (n > 0) {
		abuf = NULL;
		woff = uio->uio_loffset;
again:
		if (zfs_owner_overquota(zfsvfs, zp, B_FALSE) ||
		    zfs_owner_overquota(zfsvfs, zp, B_TRUE)) {
			if (abuf != NULL)
				dmu_return_arcbuf(abuf);
			error = EDQUOT;
			break;
		}

		if (abuf == NULL && n >= max_blksz &&
		    woff >= zp->z_size &&
		    P2PHASE(woff, max_blksz) == 0 &&
		    zp->z_blksz == max_blksz) {
			/*
			 * This write covers a full block.  "Borrow" a buffer
			 * from the dmu so that we can fill it before we enter
			 * a transaction.  This avoids the possibility of
			 * holding up the transaction if the data copy hangs
			 * up on a pagefault (e.g., from an NFS server mapping).
			 */
			size_t cbytes;

			abuf = dmu_request_arcbuf(sa_get_db(zp->z_sa_hdl),
			    max_blksz);
			ASSERT(abuf != NULL);
			ASSERT(arc_buf_size(abuf) == max_blksz);
			if (error = uiocopy(abuf->b_data, max_blksz,
			    UIO_WRITE, uio, &cbytes)) {
				dmu_return_arcbuf(abuf);
				break;
			}
			ASSERT(cbytes == max_blksz);
		}

		/*
		 * Start a transaction.
		 */
		tx = dmu_tx_create(zfsvfs->z_os);
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
		dmu_tx_hold_write(tx, zp->z_id, woff, MIN(n, max_blksz));
		zfs_sa_upgrade_txholds(tx, zp);
		error = dmu_tx_assign(tx, TXG_NOWAIT);
		if (error) {
			if (error == ERESTART) {
				dmu_tx_wait(tx);
				dmu_tx_abort(tx);
				goto again;
			}
			dmu_tx_abort(tx);
			if (abuf != NULL)
				dmu_return_arcbuf(abuf);
			break;
		}

		/*
		 * If zfs_range_lock() over-locked we grow the blocksize
		 * and then reduce the lock range.  This will only happen
		 * on the first iteration since zfs_range_reduce() will
		 * shrink down r_len to the appropriate size.
		 */
		if (rl->r_len == UINT64_MAX) {
			uint64_t new_blksz;

			if (zp->z_blksz > max_blksz) {
				ASSERT(!ISP2(zp->z_blksz));
				new_blksz = MIN(end_size, SPA_MAXBLOCKSIZE);
			} else {
				new_blksz = MIN(end_size, max_blksz);
			}
			zfs_grow_blocksize(zp, new_blksz, tx);
			zfs_range_reduce(rl, woff, n);
		}

		/*
		 * XXX - should we really limit each write to z_max_blksz?
		 * Perhaps we should use SPA_MAXBLOCKSIZE chunks?
		 */
		nbytes = MIN(n, max_blksz - P2PHASE(woff, max_blksz));

		if (woff + nbytes > zp->z_size)
			vnode_pager_setsize(vp, woff + nbytes);

		if (abuf == NULL) {
			tx_bytes = uio->uio_resid;
			error = dmu_write_uio_dbuf(sa_get_db(zp->z_sa_hdl),
			    uio, nbytes, tx);
			tx_bytes -= uio->uio_resid;
		} else {
			tx_bytes = nbytes;

			ASSERT(tx_bytes == max_blksz);
			dmu_assign_arcbuf(sa_get_db(zp->z_sa_hdl), woff, abuf, tx);
			ASSERT(tx_bytes <= uio->uio_resid);
			uioskip(uio, tx_bytes);
		}

		/*
		 * If we made no progress, we're done.  If we made even
		 * partial progress, update the znode and ZIL accordingly.
		 */
		if (tx_bytes == 0) {
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
			    (void *)&zp->z_size, sizeof (uint64_t), tx);
			dmu_tx_commit(tx);
			ASSERT(error != 0);
			break;
		}

		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime,
		    B_TRUE);

		/*
		 * Update the file size (zp_size) if it has changed;
		 * account for possible concurrent updates.
		 */
		while ((end_size = zp->z_size) < uio->uio_loffset) {
			(void) atomic_cas_64(&zp->z_size, end_size,
			    uio->uio_loffset);
			ASSERT(error == 0);
		}
		/*
		 * If we are replaying and eof is non zero then force
		 * the file size to the specified eof. Note, there's no
		 * concurrency during replay.
		 */
		if (zfsvfs->z_replay && zfsvfs->z_replay_eof != 0)
			zp->z_size = zfsvfs->z_replay_eof;

		error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);

		zfs_log_write(zilog, tx, TX_WRITE, zp, woff, tx_bytes, ioflag);
		dmu_tx_commit(tx);

		if (error != 0)
			break;
		ASSERT(tx_bytes == nbytes);
		n -= nbytes;
	}

	zfs_range_unlock(rl);

	/*
	 * If we're in replay mode, or we made no progress, return error.
	 * Otherwise, it's at least a partial write, so it's successful.
	 */
	if (zfsvfs->z_replay || uio->uio_resid == start_resid) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (ioflag & IO_SYNC ||
	    zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, zp->z_id);

	ZFS_EXIT(zfsvfs);
	return (0);
}

void
zfs_get_done(zgd_t *zgd, int error)
{
	znode_t *zp = zgd->zgd_private;
	objset_t *os = zp->z_zfsvfs->z_os;
	int vfslocked;

	if (zgd->zgd_db)
		dmu_buf_rele(zgd->zgd_db, zgd);

	zfs_range_unlock(zgd->zgd_rl);

	/*
	 * Release the vnode asynchronously as we currently have the
	 * txg stopped from syncing.
	 */
//	VN_RELE_ASYNC(ZTOV(zp), dsl_pool_vnrele_taskq(dmu_objset_pool(os)));
	zfs_zinactive(zp);

	if (error == 0 && zgd->zgd_bp)
		zil_add_block(zgd->zgd_zilog, zgd->zgd_bp);

	kmem_free(zgd, sizeof (zgd_t));
}

#ifdef DEBUG
static int zil_fault_io = 0;
#endif

/*
 * Get data to generate a TX_WRITE intent log record.
 */
int
zfs_get_data(void *arg, lr_write_t *lr, char *buf, zio_t *zio)
{
	zfsvfs_t *zfsvfs = arg;
	objset_t *os = zfsvfs->z_os;
	znode_t *zp;
	uint64_t object = lr->lr_foid;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	blkptr_t *bp = &lr->lr_blkptr;
	dmu_buf_t *db;
	zgd_t *zgd;
	int error = 0;

	ASSERT(zio != NULL);
	ASSERT(size != 0);

	/*
	 * Nothing to do if the file has been removed
	 */
	if (zfs_zget(zfsvfs, object, &zp) != 0)
		return (ENOENT);
	if (zp->z_unlinked) {
		/*
		 * Release the vnode asynchronously as we currently have the
		 * txg stopped from syncing.
		 */
		VN_RELE_ASYNC(ZTOV(zp),
		    dsl_pool_vnrele_taskq(dmu_objset_pool(os)));
		return (ENOENT);
	}

	zgd = (zgd_t *)kmem_zalloc(sizeof (zgd_t), KM_SLEEP);
	zgd->zgd_zilog = zfsvfs->z_log;
	zgd->zgd_private = zp;

	/*
	 * Write records come in two flavors: immediate and indirect.
	 * For small writes it's cheaper to store the data with the
	 * log record (immediate); for large writes it's cheaper to
	 * sync the data and get a pointer to it (indirect) so that
	 * we don't have to write the data twice.
	 */
	if (buf != NULL) { /* immediate write */
		zgd->zgd_rl = zfs_range_lock(zp, offset, size, RL_READER);
		/* test for truncation needs to be done while range locked */
		if (offset >= zp->z_size) {
			error = ENOENT;
		} else {
			error = dmu_read(os, object, offset, size, buf,
			    DMU_READ_NO_PREFETCH);
		}
		ASSERT(error == 0 || error == ENOENT);
	} else { /* indirect write */
		/*
		 * Have to lock the whole block to ensure when it's
		 * written out and it's checksum is being calculated
		 * that no one can change the data. We need to re-check
		 * blocksize after we get the lock in case it's changed!
		 */
		for (;;) {
			uint64_t blkoff;
			size = zp->z_blksz;
			blkoff = ISP2(size) ? P2PHASE(offset, size) : offset;
			offset -= blkoff;
			zgd->zgd_rl = zfs_range_lock(zp, offset, size,
			    RL_READER);
			if (zp->z_blksz == size)
				break;
			offset += blkoff;
			zfs_range_unlock(zgd->zgd_rl);
		}
		/* test for truncation needs to be done while range locked */
		if (lr->lr_offset >= zp->z_size)
			error = ENOENT;
#ifdef DEBUG
		if (zil_fault_io) {
			error = EIO;
			zil_fault_io = 0;
		}
#endif
		if (error == 0)
			error = dmu_buf_hold(os, object, offset, zgd, &db,
			    DMU_READ_NO_PREFETCH);

		if (error == 0) {
			blkptr_t *obp = dmu_buf_get_blkptr(db);
			if (obp) {
				ASSERT(BP_IS_HOLE(bp));
				*bp = *obp;
			}

			zgd->zgd_db = db;
			zgd->zgd_bp = bp;

			ASSERT(db->db_offset == offset);
			ASSERT(db->db_size == size);

			error = dmu_sync(zio, lr->lr_common.lrc_txg,
			    zfs_get_done, zgd);
			ASSERT(error || lr->lr_length <= zp->z_blksz);

			/*
			 * On success, we need to wait for the write I/O
			 * initiated by dmu_sync() to complete before we can
			 * release this dbuf.  We will finish everything up
			 * in the zfs_get_done() callback.
			 */
			if (error == 0)
				return (0);

			if (error == EALREADY) {
				lr->lr_common.lrc_txtype = TX_WRITE2;
				error = 0;
			}
		}
	}

	zfs_get_done(zgd, error);

	return (error);
}

#ifdef NOTYET
/*ARGSUSED*/
static int
zfs_access(vnode_t *vp, int mode, int flag, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (flag & V_ACE_MASK)
		error = zfs_zaccess(zp, mode, flag, B_FALSE, cr);
	else
		error = zfs_zaccess_rwx(zp, mode, flag, cr);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * If vnode is for a device return a specfs vnode instead.
 */
static int
specvp_check(vnode_t **vpp, cred_t *cr)
{
	int error = 0;

	if (IS_DEVVP(*vpp)) {
		struct vnode *svp;

		svp = specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, cr);
		VN_RELE(*vpp);
		if (svp == NULL)
			error = ENOSYS;
		*vpp = svp;
	}
	return (error);
}
#endif /* NOTYET */

static int
zfs_lookup(struct vnode *dvp, char *nm, struct vnode **vpp)
{
	znode_t *dzp = VTOZ(dvp);
	zfsvfs_t *zfsvfs = dzp->z_zfsvfs;
	struct vnode *vp;
	znode_t *zp;
	zfs_dirlock_t *dl;
	int error = 0;

	*vpp = NULL;

	if (*nm == '\0')
		return ENOENT;
	if (dvp->v_type != VDIR)
		return ENOTDIR;

	ASSERT0(!(nm[0] == '.' && nm[1] == '\0'));
	ASSERT0(!(nm[0] == '.' && nm[1] == '.' && nm[2] == '\0'));
	
	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);

	/*
	 * Check accessibility of directory.
	 */

	if (error = zfs_zaccess(dzp, ACE_EXECUTE, 0, B_FALSE, NULL)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (zfsvfs->z_utf8 && u8_validate(nm, strlen(nm),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}

	error = zfs_dirent_lock(&dl, dzp, nm, &zp, ZEXISTS | ZSHARED, NULL, NULL);
	if (error) {
		ZFS_EXIT(zfsvfs);
		return error;
	}
	zfs_dirent_unlock(dl);
	dzp->z_zn_prefetch = B_TRUE; /* enable prefetching */

	if (vget(dvp->v_mount, zp->z_id, &vp)) {
		/* found in cache */
		*vpp = vp;
		ZFS_EXIT(zfsvfs);
		return 0;
	}
	if (!vp) {
		ZFS_EXIT(zfsvfs);
		return ENOMEM;
	}
	zp->z_vnode = vp;
	vp->v_data = zp;

	vp->v_mode = zp->z_mode;
	vp->v_type = IFTOVT(vp->v_mode);
	vp->v_size = zp->z_size;

	*vpp = vp;

	ZFS_EXIT(zfsvfs);
	return error;
}

static int
zfs_create(struct vnode *dvp, char *name, mode_t mode)
{
	znode_t		*zp, *dzp = VTOZ(dvp);
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	objset_t	*os;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	ksid_t		*ksid;
	uid_t		uid;
	gid_t		gid = crgetgid(cr);
	zfs_acl_ids_t   acl_ids;
	boolean_t	fuid_dirtied;
	boolean_t	have_acl = B_FALSE;
	void		*vsecp = NULL;
	int		flag = 0;
	cred_t		*cr = CRED();
	uint64_t	txtype;
	vattr_t		va = {
		.va_mask	= AT_TYPE|AT_MODE,
		.va_type	= VREG,
		.va_mode	= mode,
	}, *vap = &va;

	uid = crgetuid(cr);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	os = zfsvfs->z_os;
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}

top:
	error = zfs_dirent_lock(&dl, dzp, name, &zp, 0, NULL, NULL);
	if (error) {
		if (have_acl)
			zfs_acl_ids_free(&acl_ids);
		if (strcmp(name, "..") == 0)
			error = EISDIR;
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (zp != NULL) {
		error = EEXIST;
		goto out;
	}

	/*
	 * Create a new file object and update the directory
	 * to reference it.
	 */
	if (error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr)) {
		if (have_acl)
			zfs_acl_ids_free(&acl_ids);
		goto out;
	}

	if (!have_acl && (error = zfs_acl_ids_create(dzp, 0, vap,
	    cr, vsecp, &acl_ids)) != 0)
		goto out;
	have_acl = B_TRUE;

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids)) {
		zfs_acl_ids_free(&acl_ids);
		error = EDQUOT;
		goto out;
	}

	tx = dmu_tx_create(os);

	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE);

	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
	if (!zfsvfs->z_use_sa &&
	    acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
		    0, acl_ids.z_aclp->z_acl_bytes);
	}
	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	(void) zfs_link_create(dl, zp, tx, ZNEW);
	txtype = zfs_log_create_txtype(Z_FILE, vsecp, vap);
	zfs_log_create(zilog, tx, txtype, dzp, zp, name,
	    vsecp, acl_ids.z_fuidp, vap);
	zfs_acl_ids_free(&acl_ids);
	dmu_tx_commit(tx);
out:
	if (dl)
		zfs_dirent_unlock(dl);

	zfs_zinactive(zp);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Remove an entry from a directory.
 *
 *	IN:	dvp	- vnode of directory to remove entry from.
 *		name	- name of entry to remove.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dvp - ctime|mtime
 *	 vp - ctime (if nlink > 0)
 */

uint64_t null_xattr = 0;

static int
zfs_remove(struct vnode *dvp, struct vnode *vp, char *name)
{
	znode_t		*zp = VTOZ(vp), *dzp = VTOZ(dvp), *zp_lock;
	znode_t		*xzp;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	uint64_t	acl_obj, xattr_obj;
	uint64_t 	xattr_obj_unlinked = 0;
	uint64_t	obj = 0;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	boolean_t	may_delete_now, delete_now = FALSE;
	boolean_t	unlinked, toobig = FALSE;
	uint64_t	txtype;
	int		error;
	int		zflg = ZEXISTS;

	// NOTE: This check has no effect: sys_unlink() checks if v_type == VDIR
	// earlier, and if so, returns EISDIR as in Linux (not EPERM).
	if (vp->v_type == VDIR)
		return EPERM;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

top:
	xattr_obj = 0;
	xzp = NULL;
	/*
	 * Attempt to lock directory; fail if entry doesn't exist.
	 */
	if (error = zfs_dirent_lock(&dl, dzp, name, &zp_lock, zflg,
	    NULL, NULL)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	assert(zp_lock == zp);

	if (error = zfs_zaccess_delete(dzp, zp, NULL))
		goto out;

	/*
	 * We may delete the znode now, or we may put it in the unlinked set;
	 * it depends on whether we're the last link, and on whether there are
	 * other holds on the vnode.  So we dmu_tx_hold() the right things to
	 * allow for either case.
	 */
	obj = zp->z_id;
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);

#ifdef TODO_XATTR
	/* are there any extended attributes? */
	error = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
	    &xattr_obj, sizeof (xattr_obj));
	if (error == 0 && xattr_obj) {
		error = zfs_zget(zfsvfs, xattr_obj, &xzp);
		ASSERT0(error);
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		dmu_tx_hold_sa(tx, xzp->z_sa_hdl, B_FALSE);
	}
#endif

	/* charge as an update -- would be nice not to charge at all */
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);

	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
#ifdef TODO_XATTR
		if (xzp)
			VN_RELE(ZTOV(xzp));
#endif
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Remove the directory entry.
	 */
	error = zfs_link_destroy(dl, zp, tx, zflg, &unlinked);

	if (error) {
		dmu_tx_commit(tx);
		goto out;
	}

	if (unlinked) {

		/*
		 * Hold z_lock so that we can make sure that the ACL obj
		 * hasn't changed.  Could have been deleted due to
		 * zfs_sa_upgrade().
		 */
		mutex_enter(&zp->z_lock);
		(void) sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
		    &xattr_obj_unlinked, sizeof (xattr_obj_unlinked));
		mutex_exit(&zp->z_lock);
		zfs_unlinked_add(zp, tx);
	}

	zfs_zinactive(zp);

	txtype = TX_REMOVE;
	zfs_log_remove(zilog, tx, txtype, dzp, name, obj);

	dmu_tx_commit(tx);
out:
	zfs_dirent_unlock(dl);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Create a new directory and insert it into dvp using the name
 * provided.  Return a pointer to the inserted directory.
 *
 *	IN:	dvp	- vnode of directory to add subdir to.
 *		dirname	- name of new directory.
 *		mode	- mode of new directory.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 *	 vp - ctime|mtime|atime updated
 */
static int
zfs_mkdir(struct vnode *dvp, char *dirname, mode_t mode)
{
	znode_t		*zp, *dzp = VTOZ(dvp);
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	uint64_t	txtype;
	dmu_tx_t	*tx;
	int		error;
	int		zf = ZNEW;
	ksid_t		*ksid;
	uid_t		uid;
	gid_t		gid = crgetgid(cr);
	zfs_acl_ids_t   acl_ids;
	boolean_t	fuid_dirtied;
	cred_t		*cr = CRED();
	vattr_t		va = {
		.va_mask	= AT_TYPE|AT_MODE,
		.va_type	= VDIR,
		.va_mode	= mode,
	}, *vap = &va;

	ASSERT(vap->va_type == VDIR);

	uid = crgetuid(cr);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (dzp->z_pflags & ZFS_XATTR) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	if (zfsvfs->z_utf8 && u8_validate(dirname,
	    strlen(dirname), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}

	if ((error = zfs_acl_ids_create(dzp, 0, vap, cr,
	    NULL, &acl_ids)) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	/*
	 * First make sure the new directory doesn't exist.
	 *
	 * Existence is checked first to make sure we don't return
	 * EACCES instead of EEXIST which can cause some applications
	 * to fail.
	 */
top:
	if (error = zfs_dirent_lock(&dl, dzp, dirname, &zp, zf,
	    NULL, NULL)) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (error = zfs_zaccess(dzp, ACE_ADD_SUBDIRECTORY, 0, B_FALSE, cr)) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids)) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (EDQUOT);
	}

	/*
	 * Add a new entry to the directory.
	 */
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, dirname);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	if (!zfsvfs->z_use_sa && acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    acl_ids.z_aclp->z_acl_bytes);
	}

	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE);

	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Create new node.
	 */
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	/*
	 * Now put new name in parent dir.
	 */
	(void) zfs_link_create(dl, zp, tx, ZNEW);

	txtype = zfs_log_create_txtype(Z_DIR, NULL, vap);
	zfs_log_create(zilog, tx, txtype, dzp, zp, dirname, NULL,
	    acl_ids.z_fuidp, vap);

	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	zfs_zinactive(zp);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (0);
}

/*
 * Remove a directory subdir entry.  If the current working
 * directory is the same as the subdir to be removed, the
 * remove will fail.
 *
 *	IN:	dvp	- vnode of directory to remove from.
 *		 vp	- vnode of directory to remove.
 *		name	- name of directory to be removed.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 */
/*ARGSUSED*/
static int
zfs_rmdir(vnode_t *dvp, vnode_t *vp, char *name)
{
	znode_t		*dzp = VTOZ(dvp), *zp = VTOZ(vp), *zp_lock;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	int		zflg = ZEXISTS;

	if (vp->v_type != VDIR)
		return ENOTDIR;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

top:
	/*
	 * Attempt to lock directory; fail if entry doesn't exist.
	 */
	if (error = zfs_dirent_lock(&dl, dzp, name, &zp_lock, zflg,
	    NULL, NULL)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	assert(zp == zp_lock);

	if (error = zfs_zaccess_delete(dzp, zp, NULL)) {
		goto out;
	}

	/*
	 * Grab a lock on the directory to make sure that noone is
	 * trying to add (or lookup) entries while we are removing it.
	 */
	rw_enter(&zp->z_name_lock, RW_WRITER);

	/*
	 * Grab a lock on the parent pointer to make sure we play well
	 * with the treewalk and directory rename code.
	 */
	rw_enter(&zp->z_parent_lock, RW_WRITER);

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);
	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		rw_exit(&zp->z_parent_lock);
		rw_exit(&zp->z_name_lock);
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	error = zfs_link_destroy(dl, zp, tx, zflg, NULL);

	if (error == 0)
		zfs_log_remove(zilog, tx, TX_RMDIR, dzp, name, ZFS_NO_OBJECT);

	dmu_tx_commit(tx);

	rw_exit(&zp->z_parent_lock);
	rw_exit(&zp->z_name_lock);

out:
	zfs_dirent_unlock(dl);

	zfs_zinactive(zp);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Note that the low 4 bits of the cookie returned by zap is always zero.
 * This allows us to use the low range for "special" directory entries:
 * We use 0 for '.', and 1 for '..'.  If this is the root of the filesystem,
 * we use the offset 2 for the '.zfs' directory.
 */
static int
zfs_readdir(struct vnode *dvp, struct file *fp, struct dirent *dir)
{
	znode_t		*zp = VTOZ(dvp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	objset_t	*os;
	zap_cursor_t	zc;
	zap_attribute_t	zap;
	uint64_t	parent;
	int		error;
	uint8_t		prefetch;
	uint8_t		type;
	int		flags = 0;
	ino64_t		objnum;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent))) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Quit if directory has been removed (posix)
	 */
	if (zp->z_unlinked != 0) {
		ZFS_EXIT(zfsvfs);
		return ENOENT;
	}

	error = 0;
	os = zfsvfs->z_os;
	prefetch = zp->z_zn_prefetch;

	/*
	 * Initialize the iterator cursor.
	 */
	if (file_offset(fp) <= 3) {
		/*
		 * Start iteration from the beginning of the directory.
		 */
		zap_cursor_init(&zc, os, zp->z_id);
	} else {
		/*
		 * The offset is a serialized cursor.
		 */
		zap_cursor_init_serialized(&zc, os, zp->z_id, file_offset(fp));
	}

	/*
	 * Special case `.', `..', and `.zfs'.
	 */
	if (file_offset(fp) == 0) {
		(void) strcpy(zap.za_name, ".");
		zap.za_normalization_conflict = 0;
		objnum = zp->z_id;
		type = DT_DIR;
	} else if (file_offset(fp) == 1) {
		(void) strcpy(zap.za_name, "..");
		zap.za_normalization_conflict = 0;
		objnum = parent;
		type = DT_DIR;
	} else if (file_offset(fp) == 2 && zfs_show_ctldir(zp)) {
		(void) strcpy(zap.za_name, ZFS_CTLDIR_NAME);
		zap.za_normalization_conflict = 0;
		objnum = ZFSCTL_INO_ROOT;
		type = DT_DIR;
	} else {
		/*
		 * Grab next entry.
		 */
		if (error = zap_cursor_retrieve(&zc, &zap))
			goto update;

		if (zap.za_integer_length != 8 ||
		    zap.za_num_integers != 1) {
			cmn_err(CE_WARN, "zap_readdir: bad directory "
			    "entry, obj = %lld, offset = %lld\n",
			    (u_longlong_t)zp->z_id,
			    (u_longlong_t)file_offset(fp));
			error = ENXIO;
			goto update;
		}

		objnum = ZFS_DIRENT_OBJ(zap.za_first_integer);
		/*
		 * MacOS X can extract the object type here such as:
		 * uint8_t type = ZFS_DIRENT_TYPE(zap.za_first_integer);
		 */
		type = ZFS_DIRENT_TYPE(zap.za_first_integer);
	}

	/*
	 * Add normal entry:
	 */
	dir->d_ino = objnum;
	(void) strlcpy(dir->d_name, zap.za_name, strlen(zap.za_name) + 1);
	dir->d_type = type;

	/* Prefetch znode */
	if (prefetch)
		dmu_prefetch(os, objnum, 0, 0);

	/*
	 * Move to the next entry, fill in the previous offset.
	 */
	if (file_offset(fp) > 2 || (file_offset(fp) == 2 && !zfs_show_ctldir(zp))) {
		zap_cursor_advance(&zc);
		file_setoffset(fp, zap_cursor_serialize(&zc));
	} else {
		file_setoffset(fp, file_offset(fp) + 1);
	}

	zp->z_zn_prefetch = B_FALSE; /* a lookup will re-enable pre-fetching */

update:
	zap_cursor_fini(&zc);

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);

	ZFS_EXIT(zfsvfs);
	return (error);
}

ulong_t zfs_fsync_sync_cnt = 4;

static int
zfs_fsync(vnode_t *vp, file_t *fp)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

#ifdef TODO
	vop_stdfsync(ap);
#endif

	(void) tsd_set(zfs_fsyncer_key, (void *)zfs_fsync_sync_cnt);

	if (zfsvfs->z_os->os_sync != ZFS_SYNC_DISABLED) {
		ZFS_ENTER(zfsvfs);
		ZFS_VERIFY_ZP(zp);
		zil_commit(zfsvfs->z_log, zp->z_id);
		ZFS_EXIT(zfsvfs);
	}

	return (0);
}


/*
 * Get the requested file attributes and place them in the provided
 * vattr structure.
 *
 *	IN:	vp	- vnode of file.
 *		vap	- va_mask identifies requested attributes.
 *			  If AT_XVATTR set, then optional attrs are requested
 *		flags	- ATTR_NOACLCHECK (CIFS server context)
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	vap	- attribute values.
 *
 *	RETURN:	0 (always succeeds)
 */
/* ARGSUSED */
static int
zfs_getattr(vnode_t *vp, vattr_t *vap)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int	error = 0;
	uint32_t blksize;
	u_longlong_t nblocks;
	uint64_t links;
	uint64_t mtime[2], ctime[2], crtime[2], rdev;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t *xoap = NULL;
	sa_bulk_attr_t bulk[4];
	int count = 0;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, &ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CRTIME(zfsvfs), NULL, &crtime, 16);
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_RDEV(zfsvfs), NULL,
		    &rdev, 8);

	if ((error = sa_bulk_lookup(zp->z_sa_hdl, bulk, count)) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Return all attributes.  It's cheaper to provide the answer
	 * than to determine whether we were asked the question.
	 */

	mutex_enter(&zp->z_lock);
	vap->va_type = IFTOVT(zp->z_mode);
	vap->va_mode = zp->z_mode & ~S_IFMT;
#ifdef sun
	vap->va_fsid = zp->z_zfsvfs->z_vfs->vfs_dev;
#else
	vap->va_fsid =  zp->z_zfsvfs->z_vfs->vfs_fsid.__val[0] |
		((dev_t) zp->z_zfsvfs->z_vfs->vfs_fsid.__val[1] << 32);
#endif
	vap->va_nodeid = zp->z_id;
	if ((vp->v_flags & VROOT) && zfs_show_ctldir(zp))
		links = zp->z_links + 1;
	else
		links = zp->z_links;
	vap->va_nlink = MIN(links, UINT32_MAX); /* nlink_t limit! */
	vap->va_size = zp->z_size;
#ifdef sun
	vap->va_rdev = vp->v_rdev;
#endif

	ZFS_TIME_DECODE(&vap->va_atime, zp->z_atime);
	ZFS_TIME_DECODE(&vap->va_mtime, mtime);
	ZFS_TIME_DECODE(&vap->va_ctime, ctime);

	mutex_exit(&zp->z_lock);

	ZFS_EXIT(zfsvfs);
	return (0);
}

/*
 * Set the file attributes to the values contained in the
 * vattr structure.
 *
 *	IN:	vp	- vnode of file to be modified.
 *		vap	- new attribute values.
 *			  If AT_XVATTR set, then optional attrs are being set
 *		flags	- ATTR_UTIME set if non-default time values provided.
 *			- ATTR_NOACLCHECK (CIFS context only).
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - ctime updated, mtime updated if size changed.
 */
/* ARGSUSED */
static int
zfs_setattr(vnode_t *vp, vattr_t *vap)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	vattr_t		oldva;
	xvattr_t	tmpxvattr;
	uint_t		mask = vap->va_mask;
	uint_t		saved_mask;
	uint64_t	saved_mode;
	int		flags = 0;
	cred_t		*cr = CRED();
	int		trim_mask = 0;
	uint64_t	new_mode;
	uint64_t	new_uid, new_gid;
	uint64_t	xattr_obj;
	uint64_t	mtime[2], ctime[2];
	znode_t		*attrzp;
	int		need_policy = FALSE;
	int		err, err2;
	zfs_fuid_info_t *fuidp = NULL;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t	*xoap;
	zfs_acl_t	*aclp;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	boolean_t	fuid_dirtied = B_FALSE;
	sa_bulk_attr_t	bulk[7], xattr_bulk[7];
	int		count = 0, xattr_count = 0;

	if (mask == 0)
		return (0);

	if (mask & AT_NOSET)
		return (EINVAL);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	zilog = zfsvfs->z_log;

	/*
	 * Make sure that if we have ephemeral uid/gid or xvattr specified
	 * that file system is at proper version level
	 */

	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (((mask & AT_UID) && IS_EPHEMERAL(vap->va_uid)) ||
	    ((mask & AT_GID) && IS_EPHEMERAL(vap->va_gid)) ||
	    (mask & AT_XVATTR))) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	if (mask & AT_SIZE && vp->v_type == VDIR) {
		ZFS_EXIT(zfsvfs);
		return (EISDIR);
	}

	if (mask & AT_SIZE && vp->v_type != VREG && vp->v_type != VFIFO) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

#ifndef __OSV__
	/*
	 * If this is an xvattr_t, then get a pointer to the structure of
	 * optional attributes.  If this is NULL, then we have a vattr_t.
	 */
	xoap = xva_getxoptattr(xvap);

	xva_init(&tmpxvattr);
#endif

	/*
	 * Immutable files can only alter immutable bit and atime
	 */
	if ((zp->z_pflags & ZFS_IMMUTABLE) &&
	    ((mask & (AT_SIZE|AT_UID|AT_GID|AT_MTIME|AT_MODE)) ||
	    ((mask & AT_XVATTR) && XVA_ISSET_REQ(xvap, XAT_CREATETIME)))) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	if ((mask & AT_SIZE) && (zp->z_pflags & ZFS_READONLY)) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	/*
	 * Verify timestamps doesn't overflow 32 bits.
	 * ZFS can handle large timestamps, but 32bit syscalls can't
	 * handle times greater than 2039.  This check should be removed
	 * once large timestamps are fully supported.
	 */
	if (mask & (AT_ATIME | AT_MTIME)) {
		if (((mask & AT_ATIME) && TIMESPEC_OVERFLOW(&vap->va_atime)) ||
		    ((mask & AT_MTIME) && TIMESPEC_OVERFLOW(&vap->va_mtime))) {
			ZFS_EXIT(zfsvfs);
			return (EOVERFLOW);
		}
	}

top:
	attrzp = NULL;
	aclp = NULL;

	/* Can this be moved to before the top label? */
	if (zfsvfs->z_vfs->vfs_flag & VFS_RDONLY) {
		ZFS_EXIT(zfsvfs);
		return (EROFS);
	}

	/*
	 * First validate permissions
	 */

	if (mask & AT_SIZE) {
		/*
		 * XXX - Note, we are not providing any open
		 * mode flags here (like FNDELAY), so we may
		 * block if there are locks present... this
		 * should be addressed in openat().
		 */
		/* XXX - would it be OK to generate a log record here? */
		err = zfs_freesp(zp, vap->va_size, 0, 0, FALSE);
		if (err) {
			ZFS_EXIT(zfsvfs);
			return (err);
		}
	}

	if (mask & (AT_ATIME|AT_MTIME) ||
	    ((mask & AT_XVATTR) && (XVA_ISSET_REQ(xvap, XAT_HIDDEN) ||
	    XVA_ISSET_REQ(xvap, XAT_READONLY) ||
	    XVA_ISSET_REQ(xvap, XAT_ARCHIVE) ||
	    XVA_ISSET_REQ(xvap, XAT_OFFLINE) ||
	    XVA_ISSET_REQ(xvap, XAT_SPARSE) ||
	    XVA_ISSET_REQ(xvap, XAT_CREATETIME) ||
	    XVA_ISSET_REQ(xvap, XAT_SYSTEM)))) {
		need_policy = zfs_zaccess(zp, ACE_WRITE_ATTRIBUTES, 0,
		    skipaclchk, cr);
	}

	if (mask & (AT_UID|AT_GID)) {
		int	idmask = (mask & (AT_UID|AT_GID));
		int	take_owner;
		int	take_group;

		/*
		 * NOTE: even if a new mode is being set,
		 * we may clear S_ISUID/S_ISGID bits.
		 */

		if (!(mask & AT_MODE))
			vap->va_mode = zp->z_mode;

		/*
		 * Take ownership or chgrp to group we are a member of
		 */

		take_owner = (mask & AT_UID) && (vap->va_uid == crgetuid(cr));
		take_group = (mask & AT_GID) &&
		    zfs_groupmember(zfsvfs, vap->va_gid, cr);

		/*
		 * If both AT_UID and AT_GID are set then take_owner and
		 * take_group must both be set in order to allow taking
		 * ownership.
		 *
		 * Otherwise, send the check through secpolicy_vnode_setattr()
		 *
		 */

		if (((idmask == (AT_UID|AT_GID)) && take_owner && take_group) ||
		    ((idmask == AT_UID) && take_owner) ||
		    ((idmask == AT_GID) && take_group)) {
			if (zfs_zaccess(zp, ACE_WRITE_OWNER, 0,
			    skipaclchk, cr) == 0) {
				/*
				 * Remove setuid/setgid for non-privileged users
				 */
				secpolicy_setid_clear(vap, vp, cr);
				trim_mask = (mask & (AT_UID|AT_GID));
			} else {
				need_policy =  TRUE;
			}
		} else {
			need_policy =  TRUE;
		}
	}

	mutex_enter(&zp->z_lock);
	oldva.va_mode = zp->z_mode;
	zfs_fuid_map_ids(zp, cr, &oldva.va_uid, &oldva.va_gid);
#ifndef __OSV__
	if (mask & AT_XVATTR) {
		/*
		 * Update xvattr mask to include only those attributes
		 * that are actually changing.
		 *
		 * the bits will be restored prior to actually setting
		 * the attributes so the caller thinks they were set.
		 */
		if (XVA_ISSET_REQ(xvap, XAT_APPENDONLY)) {
			if (xoap->xoa_appendonly !=
			    ((zp->z_pflags & ZFS_APPENDONLY) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_APPENDONLY);
				XVA_SET_REQ(&tmpxvattr, XAT_APPENDONLY);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK)) {
			if (xoap->xoa_nounlink !=
			    ((zp->z_pflags & ZFS_NOUNLINK) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NOUNLINK);
				XVA_SET_REQ(&tmpxvattr, XAT_NOUNLINK);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE)) {
			if (xoap->xoa_immutable !=
			    ((zp->z_pflags & ZFS_IMMUTABLE) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_IMMUTABLE);
				XVA_SET_REQ(&tmpxvattr, XAT_IMMUTABLE);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NODUMP)) {
			if (xoap->xoa_nodump !=
			    ((zp->z_pflags & ZFS_NODUMP) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NODUMP);
				XVA_SET_REQ(&tmpxvattr, XAT_NODUMP);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED)) {
			if (xoap->xoa_av_modified !=
			    ((zp->z_pflags & ZFS_AV_MODIFIED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_MODIFIED);
				XVA_SET_REQ(&tmpxvattr, XAT_AV_MODIFIED);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
			if ((vp->v_type != VREG &&
			    xoap->xoa_av_quarantined) ||
			    xoap->xoa_av_quarantined !=
			    ((zp->z_pflags & ZFS_AV_QUARANTINED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_QUARANTINED);
				XVA_SET_REQ(&tmpxvattr, XAT_AV_QUARANTINED);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_REPARSE)) {
			mutex_exit(&zp->z_lock);
			ZFS_EXIT(zfsvfs);
			return (EPERM);
		}

		if (need_policy == FALSE &&
		    (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP) ||
		    XVA_ISSET_REQ(xvap, XAT_OPAQUE))) {
			need_policy = TRUE;
		}
	}
#endif

	mutex_exit(&zp->z_lock);

	if (mask & AT_MODE) {
		if (zfs_zaccess(zp, ACE_WRITE_ACL, 0, skipaclchk, cr) == 0) {
			err = secpolicy_setid_setsticky_clear(vp, vap,
			    &oldva, cr);
			if (err) {
				ZFS_EXIT(zfsvfs);
				return (err);
			}
			trim_mask |= AT_MODE;
		} else {
			need_policy = TRUE;
		}
	}

	if (need_policy) {
		/*
		 * If trim_mask is set then take ownership
		 * has been granted or write_acl is present and user
		 * has the ability to modify mode.  In that case remove
		 * UID|GID and or MODE from mask so that
		 * secpolicy_vnode_setattr() doesn't revoke it.
		 */

		if (trim_mask) {
			saved_mask = vap->va_mask;
			vap->va_mask &= ~trim_mask;
			if (trim_mask & AT_MODE) {
				/*
				 * Save the mode, as secpolicy_vnode_setattr()
				 * will overwrite it with ova.va_mode.
				 */
				saved_mode = vap->va_mode;
			}
		}
		err = secpolicy_vnode_setattr(cr, vp, vap, &oldva, flags,
		    (int (*)(void *, int, cred_t *))zfs_zaccess_unix, zp);
		if (err) {
			ZFS_EXIT(zfsvfs);
			return (err);
		}

		if (trim_mask) {
			vap->va_mask |= saved_mask;
			if (trim_mask & AT_MODE) {
				/*
				 * Recover the mode after
				 * secpolicy_vnode_setattr().
				 */
				vap->va_mode = saved_mode;
			}
		}
	}

	/*
	 * secpolicy_vnode_setattr, or take ownership may have
	 * changed va_mask
	 */
	mask = vap->va_mask;

	if ((mask & (AT_UID | AT_GID))) {
		err = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
		    &xattr_obj, sizeof (xattr_obj));

		if (err == 0 && xattr_obj) {
			err = zfs_zget(zp->z_zfsvfs, xattr_obj, &attrzp);
			if (err)
				goto out2;
		}
		if (mask & AT_UID) {
			new_uid = zfs_fuid_create(zfsvfs,
			    (uint64_t)vap->va_uid, cr, ZFS_OWNER, &fuidp);
			if (new_uid != zp->z_uid &&
			    zfs_fuid_overquota(zfsvfs, B_FALSE, new_uid)) {
				if (attrzp)
					VN_RELE(ZTOV(attrzp));
				err = EDQUOT;
				goto out2;
			}
		}

		if (mask & AT_GID) {
			new_gid = zfs_fuid_create(zfsvfs, (uint64_t)vap->va_gid,
			    cr, ZFS_GROUP, &fuidp);
			if (new_gid != zp->z_gid &&
			    zfs_fuid_overquota(zfsvfs, B_TRUE, new_gid)) {
				if (attrzp)
					VN_RELE(ZTOV(attrzp));
				err = EDQUOT;
				goto out2;
			}
		}
	}
	tx = dmu_tx_create(zfsvfs->z_os);

	if (mask & AT_MODE) {
		uint64_t pmode = zp->z_mode;
		uint64_t acl_obj;
		new_mode = (pmode & S_IFMT) | (vap->va_mode & ~S_IFMT);

		if (zp->z_zfsvfs->z_acl_mode == ZFS_ACL_RESTRICTED &&
		    !(zp->z_pflags & ZFS_ACL_TRIVIAL)) {
			err = EPERM;
			goto out;
		}

		if (err = zfs_acl_chmod_setattr(zp, &aclp, new_mode))
			goto out;

		mutex_enter(&zp->z_lock);
		if (!zp->z_is_sa && ((acl_obj = zfs_external_acl(zp)) != 0)) {
			/*
			 * Are we upgrading ACL from old V0 format
			 * to V1 format?
			 */
			if (zfsvfs->z_version >= ZPL_VERSION_FUID &&
			    zfs_znode_acl_version(zp) ==
			    ZFS_ACL_VERSION_INITIAL) {
				dmu_tx_hold_free(tx, acl_obj, 0,
				    DMU_OBJECT_END);
				dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
				    0, aclp->z_acl_bytes);
			} else {
				dmu_tx_hold_write(tx, acl_obj, 0,
				    aclp->z_acl_bytes);
			}
		} else if (!zp->z_is_sa && aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
			    0, aclp->z_acl_bytes);
		}
		mutex_exit(&zp->z_lock);
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
	} else {
		if ((mask & AT_XVATTR) &&
		    XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP))
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		else
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	}

	if (attrzp) {
		dmu_tx_hold_sa(tx, attrzp->z_sa_hdl, B_FALSE);
	}

	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);

	zfs_sa_upgrade_txholds(tx, zp);

	err = dmu_tx_assign(tx, TXG_NOWAIT);
	if (err) {
		if (err == ERESTART)
			dmu_tx_wait(tx);
		goto out;
	}

	count = 0;
	/*
	 * Set each attribute requested.
	 * We group settings according to the locks they need to acquire.
	 *
	 * Note: you cannot set ctime directly, although it will be
	 * updated as a side-effect of calling this function.
	 */


	if (mask & (AT_UID|AT_GID|AT_MODE))
		mutex_enter(&zp->z_acl_lock);
	mutex_enter(&zp->z_lock);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));

	if (attrzp) {
		if (mask & (AT_UID|AT_GID|AT_MODE))
			mutex_enter(&attrzp->z_acl_lock);
		mutex_enter(&attrzp->z_lock);
		SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
		    SA_ZPL_FLAGS(zfsvfs), NULL, &attrzp->z_pflags,
		    sizeof (attrzp->z_pflags));
	}

	if (mask & (AT_UID|AT_GID)) {

		if (mask & AT_UID) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
			    &new_uid, sizeof (new_uid));
			zp->z_uid = new_uid;
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_UID(zfsvfs), NULL, &new_uid,
				    sizeof (new_uid));
				attrzp->z_uid = new_uid;
			}
		}

		if (mask & AT_GID) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs),
			    NULL, &new_gid, sizeof (new_gid));
			zp->z_gid = new_gid;
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_GID(zfsvfs), NULL, &new_gid,
				    sizeof (new_gid));
				attrzp->z_gid = new_gid;
			}
		}
		if (!(mask & AT_MODE)) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs),
			    NULL, &new_mode, sizeof (new_mode));
			new_mode = zp->z_mode;
		}
		err = zfs_acl_chown_setattr(zp);
		ASSERT(err == 0);
		if (attrzp) {
			err = zfs_acl_chown_setattr(attrzp);
			ASSERT(err == 0);
		}
	}

	if (mask & AT_MODE) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL,
		    &new_mode, sizeof (new_mode));
		zp->z_mode = new_mode;
		ASSERT3U((uintptr_t)aclp, !=, 0);
		err = zfs_aclset_common(zp, aclp, cr, tx);
		ASSERT0(err);
		if (zp->z_acl_cached)
			zfs_acl_free(zp->z_acl_cached);
		zp->z_acl_cached = aclp;
		aclp = NULL;
	}


	if (mask & AT_ATIME) {
		ZFS_TIME_ENCODE(&vap->va_atime, zp->z_atime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL,
		    &zp->z_atime, sizeof (zp->z_atime));
	}

	if (mask & AT_MTIME) {
		ZFS_TIME_ENCODE(&vap->va_mtime, mtime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
		    mtime, sizeof (mtime));
	}

	/* XXX - shouldn't this be done *before* the ATIME/MTIME checks? */
	if (mask & AT_SIZE && !(mask & AT_MTIME)) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs),
		    NULL, mtime, sizeof (mtime));
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    &ctime, sizeof (ctime));
		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime,
		    B_TRUE);
	} else if (mask != 0) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    &ctime, sizeof (ctime));
		zfs_tstamp_update_setup(zp, STATE_CHANGED, mtime, ctime,
		    B_TRUE);
		if (attrzp) {
			SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
			    SA_ZPL_CTIME(zfsvfs), NULL,
			    &ctime, sizeof (ctime));
			zfs_tstamp_update_setup(attrzp, STATE_CHANGED,
			    mtime, ctime, B_TRUE);
		}
	}
	/*
	 * Do this after setting timestamps to prevent timestamp
	 * update from toggling bit
	 */
#ifndef __OSV__
	if (xoap && (mask & AT_XVATTR)) {

		/*
		 * restore trimmed off masks
		 * so that return masks can be set for caller.
		 */

		if (XVA_ISSET_REQ(&tmpxvattr, XAT_APPENDONLY)) {
			XVA_SET_REQ(xvap, XAT_APPENDONLY);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_NOUNLINK)) {
			XVA_SET_REQ(xvap, XAT_NOUNLINK);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_IMMUTABLE)) {
			XVA_SET_REQ(xvap, XAT_IMMUTABLE);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_NODUMP)) {
			XVA_SET_REQ(xvap, XAT_NODUMP);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_AV_MODIFIED)) {
			XVA_SET_REQ(xvap, XAT_AV_MODIFIED);
		}
		if (XVA_ISSET_REQ(&tmpxvattr, XAT_AV_QUARANTINED)) {
			XVA_SET_REQ(xvap, XAT_AV_QUARANTINED);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP))
			ASSERT(vp->v_type == VREG);

		zfs_xvattr_set(zp, xvap, tx);
	}
#endif
	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	if (mask != 0)
		zfs_log_setattr(zilog, tx, TX_SETATTR, zp, vap, mask, fuidp);

	mutex_exit(&zp->z_lock);
	if (mask & (AT_UID|AT_GID|AT_MODE))
		mutex_exit(&zp->z_acl_lock);

	if (attrzp) {
		if (mask & (AT_UID|AT_GID|AT_MODE))
			mutex_exit(&attrzp->z_acl_lock);
		mutex_exit(&attrzp->z_lock);
	}
out:
	if (err == 0 && attrzp) {
		err2 = sa_bulk_update(attrzp->z_sa_hdl, xattr_bulk,
		    xattr_count, tx);
		ASSERT(err2 == 0);
	}

	if (attrzp)
		VN_RELE(ZTOV(attrzp));
	if (aclp)
		zfs_acl_free(aclp);

	if (fuidp) {
		zfs_fuid_info_free(fuidp);
		fuidp = NULL;
	}

	if (err) {
		dmu_tx_abort(tx);
		if (err == ERESTART)
			goto top;
	} else {
		err2 = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		dmu_tx_commit(tx);
	}

out2:
	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (err);
}

static int
zfs_truncate(struct vnode *vp, off_t new_size)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	uint64_t	mtime[2], ctime[2];
	int		err, err2;
	sa_bulk_attr_t	bulk[7];
	int		count = 0;
	struct vattr	va = {
		.va_mask	= AT_SIZE,
		.va_size	= new_size,
	};

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	zilog = zfsvfs->z_log;

	switch (vp->v_type) {
	case VDIR:
		ZFS_EXIT(zfsvfs);
		return (EISDIR);
	case VREG:
	case VFIFO:
		break;
	default:
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	if (zp->z_pflags & ZFS_IMMUTABLE) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	if (zp->z_pflags & ZFS_READONLY) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

top:
	/* Can this be moved to before the top label? */
	if (zfsvfs->z_vfs->vfs_flag & VFS_RDONLY) {
		ZFS_EXIT(zfsvfs);
		return (EROFS);
	}

	err = zfs_freesp(zp, new_size, 0, 0, FALSE);
	if (err) {
		ZFS_EXIT(zfsvfs);
		return (err);
	}

	tx = dmu_tx_create(zfsvfs->z_os);

	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);

	zfs_sa_upgrade_txholds(tx, zp);

	err = dmu_tx_assign(tx, TXG_NOWAIT);
	if (err) {
		if (err == ERESTART)
			dmu_tx_wait(tx);
		goto out;
	}

	count = 0;
	mutex_enter(&zp->z_lock);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs),
	    NULL, mtime, sizeof (mtime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
	    &ctime, sizeof (ctime));
	zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime,
	    B_TRUE);

	zfs_log_setattr(zilog, tx, TX_SETATTR, zp, &va, AT_SIZE, NULL);

	mutex_exit(&zp->z_lock);
out:
	if (err) {
		dmu_tx_abort(tx);
		if (err == ERESTART)
			goto top;
	} else {
		err2 = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		dmu_tx_commit(tx);
	}

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (err);
}

typedef struct zfs_zlock {
	krwlock_t	*zl_rwlock;	/* lock we acquired */
	znode_t		*zl_znode;	/* znode we held */
	struct zfs_zlock *zl_next;	/* next in list */
} zfs_zlock_t;

/*
 * Drop locks and release vnodes that were held by zfs_rename_lock().
 */
static void
zfs_rename_unlock(zfs_zlock_t **zlpp)
{
	zfs_zlock_t *zl;

	while ((zl = *zlpp) != NULL) {
		if (zl->zl_znode != NULL)
			zfs_zinactive(zl->zl_znode);
		rw_exit(zl->zl_rwlock);
		*zlpp = zl->zl_next;
		kmem_free(zl, sizeof (*zl));
	}
}

/*
 * Search back through the directory tree, using the ".." entries.
 * Lock each directory in the chain to prevent concurrent renames.
 * Fail any attempt to move a directory into one of its own descendants.
 * XXX - z_parent_lock can overlap with map or grow locks
 */
static int
zfs_rename_lock(znode_t *szp, znode_t *tdzp, znode_t *sdzp, zfs_zlock_t **zlpp)
{
	zfs_zlock_t	*zl;
	znode_t		*zp = tdzp;
	uint64_t	rootid = zp->z_zfsvfs->z_root;
	uint64_t	oidp = zp->z_id;
	krwlock_t	*rwlp = &szp->z_parent_lock;
	krw_t		rw = RW_WRITER;

	/*
	 * First pass write-locks szp and compares to zp->z_id.
	 * Later passes read-lock zp and compare to zp->z_parent.
	 */
	do {
		if (!rw_tryenter(rwlp, rw)) {
			/*
			 * Another thread is renaming in this path.
			 * Note that if we are a WRITER, we don't have any
			 * parent_locks held yet.
			 */
			if (rw == RW_READER && zp->z_id > szp->z_id) {
				/*
				 * Drop our locks and restart
				 */
				zfs_rename_unlock(&zl);
				*zlpp = NULL;
				zp = tdzp;
				oidp = zp->z_id;
				rwlp = &szp->z_parent_lock;
				rw = RW_WRITER;
				continue;
			} else {
				/*
				 * Wait for other thread to drop its locks
				 */
				rw_enter(rwlp, rw);
			}
		}

		zl = kmem_alloc(sizeof (*zl), KM_SLEEP);
		zl->zl_rwlock = rwlp;
		zl->zl_znode = NULL;
		zl->zl_next = *zlpp;
		*zlpp = zl;

		if (oidp == szp->z_id)		/* We're a descendant of szp */
			return (EINVAL);

		if (oidp == rootid)		/* We've hit the top */
			return (0);

		if (rw == RW_READER) {		/* i.e. not the first pass */
			int error = zfs_zget(zp->z_zfsvfs, oidp, &zp);
			if (error)
				return (error);
			zl->zl_znode = zp;
		}
		(void) sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zp->z_zfsvfs),
		    &oidp, sizeof (oidp));
		rwlp = &zp->z_parent_lock;
		rw = RW_READER;

	} while (zp->z_id != sdzp->z_id);

	return (0);
}

/*
 * Move an entry from the provided source directory to the target
 * directory.  Change the entry name as indicated.
 *
 *	IN:	sdvp	- Source directory containing the "old entry".
 *		svp	- Source vnode
 *		snm	- Old entry name.
 *		tdvp	- Target directory to contain the "new entry".
 *		tvp	- Target vnode
 *		tnm	- New entry name.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	sdvp,tdvp - ctime|mtime updated
 */
static int
zfs_rename(struct vnode *sdvp, struct vnode *svp, char *snm,
	     struct vnode *tdvp, struct vnode *tvp, char *tnm)
{
	znode_t		*tdzp, *szp, *tzp;
	znode_t		*sdzp = VTOZ(sdvp);
	zfsvfs_t	*zfsvfs = sdzp->z_zfsvfs;
	zilog_t		*zilog;
	zfs_dirlock_t	*sdl, *tdl;
	dmu_tx_t	*tx;
	zfs_zlock_t	*zl;
	int		cmp, serr, terr;
	int		error = 0;
	int		zflg = 0;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(sdzp);
	zilog = zfsvfs->z_log;

	if (tdvp->v_vfsp != sdvp->v_vfsp) {
		ZFS_EXIT(zfsvfs);
		return (EXDEV);
	}

#ifdef TODO_ZFSCTL
	if (zfsctl_is_node(tdvp)) {
		ZFS_EXIT(zfsvfs);
		return (EXDEV);
	}
#endif

	tdzp = VTOZ(tdvp);
	ZFS_VERIFY_ZP(tdzp);
	if (zfsvfs->z_utf8 && u8_validate(tnm,
	    strlen(tnm), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}

top:
	szp = NULL;
	tzp = NULL;
	zl = NULL;

	/*
	 * This is to prevent the creation of links into attribute space
	 * by renaming a linked file into/outof an attribute directory.
	 * See the comment in zfs_link() for why this is considered bad.
	 */
	if ((tdzp->z_pflags & ZFS_XATTR) != (sdzp->z_pflags & ZFS_XATTR)) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	/*
	 * Lock source and target directory entries.  To prevent deadlock,
	 * a lock ordering must be defined.  We lock the directory with
	 * the smallest object id first, or if it's a tie, the one with
	 * the lexically first name.
	 */
	if (sdzp->z_id < tdzp->z_id) {
		cmp = -1;
	} else if (sdzp->z_id > tdzp->z_id) {
		cmp = 1;
	} else {
		/*
		 * First compare the two name arguments without
		 * considering any case folding.
		 */
		int nofold = (zfsvfs->z_norm & ~U8_TEXTPREP_TOUPPER);

		cmp = u8_strcmp(snm, tnm, 0, nofold, U8_UNICODE_LATEST, &error);
		ASSERT(error == 0 || !zfsvfs->z_utf8);
		if (cmp == 0) {
			/*
			 * POSIX: "If the old argument and the new argument
			 * both refer to links to the same existing file,
			 * the rename() function shall return successfully
			 * and perform no other action."
			 */
			ZFS_EXIT(zfsvfs);
			return (0);
		}
		/*
		 * If the file system is case-folding, then we may
		 * have some more checking to do.  A case-folding file
		 * system is either supporting mixed case sensitivity
		 * access or is completely case-insensitive.  Note
		 * that the file system is always case preserving.
		 *
		 * In mixed sensitivity mode case sensitive behavior
		 * is the default.  FIGNORECASE must be used to
		 * explicitly request case insensitive behavior.
		 *
		 * If the source and target names provided differ only
		 * by case (e.g., a request to rename 'tim' to 'Tim'),
		 * we will treat this as a special case in the
		 * case-insensitive mode: as long as the source name
		 * is an exact match, we will allow this to proceed as
		 * a name-change request.
		 */
		if ((zfsvfs->z_case == ZFS_CASE_INSENSITIVE) &&
		    u8_strcmp(snm, tnm, 0, zfsvfs->z_norm, U8_UNICODE_LATEST,
		    &error) == 0) {
			/*
			 * case preserving rename request, require exact
			 * name matches
			 */
			zflg |= ZCIEXACT;
			zflg &= ~ZCILOOK;
		}
	}

	/*
	 * If the source and destination directories are the same, we should
	 * grab the z_name_lock of that directory only once.
	 */
	if (sdzp == tdzp) {
		zflg |= ZHAVELOCK;
		rw_enter(&sdzp->z_name_lock, RW_READER);
	}

	if (cmp < 0) {
		serr = zfs_dirent_lock(&sdl, sdzp, snm, &szp,
		    ZEXISTS | zflg, NULL, NULL);
		terr = zfs_dirent_lock(&tdl,
		    tdzp, tnm, &tzp, ZRENAMING | zflg, NULL, NULL);
	} else {
		terr = zfs_dirent_lock(&tdl,
		    tdzp, tnm, &tzp, zflg, NULL, NULL);
		serr = zfs_dirent_lock(&sdl,
		    sdzp, snm, &szp, ZEXISTS | ZRENAMING | zflg,
		    NULL, NULL);
	}

	if (serr) {
		/*
		 * Source entry invalid or not there.
		 */
		if (!terr) {
			zfs_dirent_unlock(tdl);
			if (tzp)
				zfs_zinactive(tzp);
		}

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		/*
		 * FreeBSD: In OpenSolaris they only check if rename source is
		 * ".." here, because "." is handled in their lookup. This is
		 * not the case for FreeBSD, so we check for "." explicitly.
		 */
		if (strcmp(snm, ".") == 0 || strcmp(snm, "..") == 0)
			serr = EINVAL;
		ZFS_EXIT(zfsvfs);
		return (serr);
	}
	if (terr) {
		zfs_dirent_unlock(sdl);
		zfs_zinactive(szp);

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		if (strcmp(tnm, "..") == 0)
			terr = EINVAL;
		ZFS_EXIT(zfsvfs);
		return (terr);
	}

	/*
	 * Must have write access at the source to remove the old entry
	 * and write access at the target to create the new entry.
	 * Note that if target and source are the same, this can be
	 * done in a single check.
	 */

	if (error = zfs_zaccess_rename(sdzp, szp, tdzp, tzp, kcred))
		goto out;

	if (S_ISDIR(szp->z_mode)) {
		/*
		 * Check to make sure rename is valid.
		 * Can't do a move like this: /usr/a/b to /usr/a/b/c/d
		 */
		if (error = zfs_rename_lock(szp, tdzp, sdzp, &zl))
			goto out;
	}

	/*
	 * Does target exist?
	 */
	if (tzp) {
		/*
		 * Source and target must be the same type.
		 */
		if (S_ISDIR(szp->z_mode)) {
			if (!S_ISDIR(tzp->z_mode)) {
				error = ENOTDIR;
				goto out;
			}
		} else {
			if (S_ISDIR(tzp->z_mode)) {
				error = EISDIR;
				goto out;
			}
		}
		/*
		 * POSIX dictates that when the source and target
		 * entries refer to the same file object, rename
		 * must do nothing and exit without error.
		 */
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
	if (tzp) {
		dmu_tx_hold_sa(tx, tzp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, tzp);
	}

	zfs_sa_upgrade_txholds(tx, szp);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		if (zl != NULL)
			zfs_rename_unlock(&zl);
		zfs_dirent_unlock(sdl);
		zfs_dirent_unlock(tdl);

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		zfs_zinactive(szp);
		if (tzp)
			zfs_zinactive(tzp);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (tzp)	/* Attempt to remove the existing target */
		error = zfs_link_destroy(tdl, tzp, tx, zflg, NULL);

	if (error == 0) {
		error = zfs_link_create(tdl, szp, tx, ZRENAMING);
		if (error == 0) {
			szp->z_pflags |= ZFS_AV_MODIFIED;

			error = sa_update(szp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
			    (void *)&szp->z_pflags, sizeof (uint64_t), tx);
			ASSERT0(error);

			error = zfs_link_destroy(sdl, szp, tx, ZRENAMING, NULL);
			if (error == 0) {
				zfs_log_rename(zilog, tx, TX_RENAME, sdzp,
				    sdl->dl_name, tdzp, tdl->dl_name, szp);
			} else {
				/*
				 * At this point, we have successfully created
				 * the target name, but have failed to remove
				 * the source name.  Since the create was done
				 * with the ZRENAMING flag, there are
				 * complications; for one, the link count is
				 * wrong.  The easiest way to deal with this
				 * is to remove the newly created target, and
				 * return the original error.  This must
				 * succeed; fortunately, it is very unlikely to
				 * fail, since we just created it.
				 */
				VERIFY3U(zfs_link_destroy(tdl, szp, tx,
				    ZRENAMING, NULL), ==, 0);
			}
		}
	}

	dmu_tx_commit(tx);
out:
	if (zl != NULL)
		zfs_rename_unlock(&zl);

	zfs_dirent_unlock(sdl);
	zfs_dirent_unlock(tdl);

	if (sdzp == tdzp)
		rw_exit(&sdzp->z_name_lock);


	zfs_zinactive(szp);
	if (tzp)
		zfs_zinactive(tzp);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);

	return (error);
}

/*
 * Insert the indicated symbolic reference entry into the directory.
 *
 *	IN:	dvp	- Directory to contain new symbolic link.
 *		link	- Name for new symlink entry.
 *		vap	- Attributes of new entry.
 *		target	- Target path of new symlink.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *		flags	- case flags
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	dvp - ctime|mtime updated
 */
/*ARGSUSED*/
static int
zfs_symlink(vnode_t *dvp, char *name, char *link)
{
	znode_t		*zp, *dzp = VTOZ(dvp);
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	uint64_t	len = strlen(link);
	int		error;
	int		zflg = ZNEW;
	zfs_acl_ids_t	acl_ids;
	boolean_t	fuid_dirtied;
	cred_t		*cr = CRED();
	uint64_t	txtype = TX_SYMLINK;
	vattr_t		va = {
		.va_mask	= AT_TYPE|AT_MODE,
		.va_type	= VLNK,
		.va_size	= len,

		/* symlink permissions are irrelevant */
		.va_mode	= S_IRWXU|S_IRWXG|S_IRWXO,
	}, *vap = &va;

	ASSERT(vap->va_type == VLNK);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}

	if (len > MAXPATHLEN) {
		ZFS_EXIT(zfsvfs);
		return (ENAMETOOLONG);
	}

	if ((error = zfs_acl_ids_create(dzp, 0,
	    vap, cr, NULL, &acl_ids)) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}
top:
	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg, NULL, NULL);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr)) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids)) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		ZFS_EXIT(zfsvfs);
		return (EDQUOT);
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, MAX(1, len));
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE + len);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
	if (!zfsvfs->z_use_sa && acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    acl_ids.z_aclp->z_acl_bytes);
	}
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	/*
	 * Create a new object for the symlink.
	 * for version 4 ZPL datsets the symlink will be an SA attribute
	 */
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	mutex_enter(&zp->z_lock);
	if (zp->z_is_sa)
		error = sa_update(zp->z_sa_hdl, SA_ZPL_SYMLINK(zfsvfs),
		    link, len, tx);
	else
		zfs_sa_symlink(zp, link, len, tx);
	mutex_exit(&zp->z_lock);

	zp->z_size = len;
	(void) sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
	    &zp->z_size, sizeof (zp->z_size), tx);
	/*
	 * Insert the new object into the directory.
	 */
	(void) zfs_link_create(dl, zp, tx, ZNEW);

	zfs_log_symlink(zilog, tx, txtype, dzp, zp, name, link);

	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	zfs_zinactive(zp);

	zfs_dirent_unlock(dl);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Return, in the buffer contained in the provided uio structure,
 * the symbolic path referred to by vp.
 *
 *	IN:	vp	- vnode of symbolic link.
 *		uoip	- structure to contain the link path.
 *		cr	- credentials of caller.
 *		ct	- caller context
 *
 *	OUT:	uio	- structure to contain the link path.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - atime updated
 */
/* ARGSUSED */
static int
zfs_readlink(vnode_t *vp, uio_t *uio)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	int		error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	mutex_enter(&zp->z_lock);
	if (zp->z_is_sa)
		error = sa_lookup_uio(zp->z_sa_hdl,
		    SA_ZPL_SYMLINK(zfsvfs), uio);
	else
		error = zfs_sa_readlink(zp, uio);
	mutex_exit(&zp->z_lock);

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*
 * Insert a new entry into directory tdvp referencing svp.
 *
 *	IN:	tdvp	- Directory to contain new entry.
 *		svp	- vnode of new entry.
 *		name	- name of new entry.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	tdvp - ctime|mtime updated
 *	 svp - ctime updated
 */
/* ARGSUSED */
static int
zfs_link(vnode_t *tdvp, vnode_t *svp, char *name)
{
	znode_t		*dzp = VTOZ(tdvp);
	znode_t		*tzp, *szp;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	vnode_t		*realvp;
	int		error;
	int		zf = ZNEW;
	uint64_t	parent;
	uid_t		owner;
	int 		flags = 0;

	ASSERT(tdvp->v_type == VDIR);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(dzp);
	zilog = zfsvfs->z_log;

	/*
	 * POSIX dictates that we return EPERM here.
	 * Better choices include ENOTSUP or EISDIR.
	 */
	if (svp->v_type == VDIR) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

#ifdef TODO_ZFSCTL
	if (svp->v_vfsp != tdvp->v_vfsp || zfsctl_is_node(svp)) {
#else
	if (svp->v_vfsp != tdvp->v_vfsp) {
#endif
		ZFS_EXIT(zfsvfs);
		return (EXDEV);
	}

	szp = VTOZ(svp);
	ZFS_VERIFY_ZP(szp);

	/* Prevent links to .zfs/shares files */

	if ((error = sa_lookup(szp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (uint64_t))) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}
	if (parent == zfsvfs->z_shares_dir) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	if (zfsvfs->z_utf8 && u8_validate(name,
	    strlen(name), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		ZFS_EXIT(zfsvfs);
		return (EILSEQ);
	}

	/*
	 * We do not support links between attributes and non-attributes
	 * because of the potential security risk of creating links
	 * into "normal" file space in order to circumvent restrictions
	 * imposed in attribute space.
	 */
	if ((szp->z_pflags & ZFS_XATTR) != (dzp->z_pflags & ZFS_XATTR)) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

top:
	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lock(&dl, dzp, name, &tzp, zf, NULL, NULL);
	if (error) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	zfs_sa_upgrade_txholds(tx, szp);
	zfs_sa_upgrade_txholds(tx, dzp);
	error = dmu_tx_assign(tx, TXG_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	error = zfs_link_create(dl, szp, tx, 0);

	if (error == 0) {
		uint64_t txtype = TX_LINK;
		zfs_log_link(zilog, tx, txtype, dzp, szp, name);
	}

	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

#ifdef sun
/*
 * zfs_null_putapage() is used when the file system has been force
 * unmounted. It just drops the pages.
 */
/* ARGSUSED */
static int
zfs_null_putapage(vnode_t *vp, page_t *pp, u_offset_t *offp,
		size_t *lenp, int flags, cred_t *cr)
{
	pvn_write_done(pp, B_INVAL|B_FORCE|B_ERROR);
	return (0);
}

/*
 * Push a page out to disk, klustering if possible.
 *
 *	IN:	vp	- file to push page to.
 *		pp	- page to push.
 *		flags	- additional flags.
 *		cr	- credentials of caller.
 *
 *	OUT:	offp	- start of range pushed.
 *		lenp	- len of range pushed.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * NOTE: callers must have locked the page to be pushed.  On
 * exit, the page (and all other pages in the kluster) must be
 * unlocked.
 */
/* ARGSUSED */
static int
zfs_putapage(vnode_t *vp, page_t *pp, u_offset_t *offp,
		size_t *lenp, int flags, cred_t *cr)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	dmu_tx_t	*tx;
	u_offset_t	off, koff;
	size_t		len, klen;
	int		err;

	off = pp->p_offset;
	len = PAGESIZE;
	/*
	 * If our blocksize is bigger than the page size, try to kluster
	 * multiple pages so that we write a full block (thus avoiding
	 * a read-modify-write).
	 */
	if (off < zp->z_size && zp->z_blksz > PAGESIZE) {
		klen = P2ROUNDUP((ulong_t)zp->z_blksz, PAGESIZE);
		koff = ISP2(klen) ? P2ALIGN(off, (u_offset_t)klen) : 0;
		ASSERT(koff <= zp->z_size);
		if (koff + klen > zp->z_size)
			klen = P2ROUNDUP(zp->z_size - koff, (uint64_t)PAGESIZE);
		pp = pvn_write_kluster(vp, pp, &off, &len, koff, klen, flags);
	}
	ASSERT3U(btop(len), ==, btopr(len));

	/*
	 * Can't push pages past end-of-file.
	 */
	if (off >= zp->z_size) {
		/* ignore all pages */
		err = 0;
		goto out;
	} else if (off + len > zp->z_size) {
		int npages = btopr(zp->z_size - off);
		page_t *trunc;

		page_list_break(&pp, &trunc, npages);
		/* ignore pages past end of file */
		if (trunc)
			pvn_write_done(trunc, flags);
		len = zp->z_size - off;
	}

	if (zfs_owner_overquota(zfsvfs, zp, B_FALSE) ||
	    zfs_owner_overquota(zfsvfs, zp, B_TRUE)) {
		err = EDQUOT;
		goto out;
	}
top:
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_write(tx, zp->z_id, off, len);

	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	err = dmu_tx_assign(tx, TXG_NOWAIT);
	if (err != 0) {
		if (err == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		goto out;
	}

	if (zp->z_blksz <= PAGESIZE) {
		caddr_t va = zfs_map_page(pp, S_READ);
		ASSERT3U(len, <=, PAGESIZE);
		dmu_write(zfsvfs->z_os, zp->z_id, off, len, va, tx);
		zfs_unmap_page(pp, va);
	} else {
		err = dmu_write_pages(zfsvfs->z_os, zp->z_id, off, len, pp, tx);
	}

	if (err == 0) {
		uint64_t mtime[2], ctime[2];
		sa_bulk_attr_t bulk[3];
		int count = 0;

		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
		    &mtime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    &ctime, 16);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
		    &zp->z_pflags, 8);
		zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime,
		    B_TRUE);
		zfs_log_write(zfsvfs->z_log, tx, TX_WRITE, zp, off, len, 0);
	}
	dmu_tx_commit(tx);

out:
	pvn_write_done(pp, (err ? B_ERROR : 0) | flags);
	if (offp)
		*offp = off;
	if (lenp)
		*lenp = len;

	return (err);
}

/*
 * Copy the portion of the file indicated from pages into the file.
 * The pages are stored in a page list attached to the files vnode.
 *
 *	IN:	vp	- vnode of file to push page data to.
 *		off	- position in file to put data.
 *		len	- amount of data to write.
 *		flags	- flags to control the operation.
 *		cr	- credentials of caller.
 *		ct	- caller context.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - ctime|mtime updated
 */
/*ARGSUSED*/
static int
zfs_putpage(vnode_t *vp, offset_t off, size_t len, int flags, cred_t *cr,
    caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	page_t		*pp;
	size_t		io_len;
	u_offset_t	io_off;
	uint_t		blksz;
	rl_t		*rl;
	int		error = 0;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/*
	 * Align this request to the file block size in case we kluster.
	 * XXX - this can result in pretty aggresive locking, which can
	 * impact simultanious read/write access.  One option might be
	 * to break up long requests (len == 0) into block-by-block
	 * operations to get narrower locking.
	 */
	blksz = zp->z_blksz;
	if (ISP2(blksz))
		io_off = P2ALIGN_TYPED(off, blksz, u_offset_t);
	else
		io_off = 0;
	if (len > 0 && ISP2(blksz))
		io_len = P2ROUNDUP_TYPED(len + (off - io_off), blksz, size_t);
	else
		io_len = 0;

	if (io_len == 0) {
		/*
		 * Search the entire vp list for pages >= io_off.
		 */
		rl = zfs_range_lock(zp, io_off, UINT64_MAX, RL_WRITER);
		error = pvn_vplist_dirty(vp, io_off, zfs_putapage, flags, cr);
		goto out;
	}
	rl = zfs_range_lock(zp, io_off, io_len, RL_WRITER);

	if (off > zp->z_size) {
		/* past end of file */
		zfs_range_unlock(rl);
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	len = MIN(io_len, P2ROUNDUP(zp->z_size, PAGESIZE) - io_off);

	for (off = io_off; io_off < off + len; io_off += io_len) {
		if ((flags & B_INVAL) || ((flags & B_ASYNC) == 0)) {
			pp = page_lookup(vp, io_off,
			    (flags & (B_INVAL | B_FREE)) ? SE_EXCL : SE_SHARED);
		} else {
			pp = page_lookup_nowait(vp, io_off,
			    (flags & B_FREE) ? SE_EXCL : SE_SHARED);
		}

		if (pp != NULL && pvn_getdirty(pp, flags)) {
			int err;

			/*
			 * Found a dirty page to push
			 */
			err = zfs_putapage(vp, pp, &io_off, &io_len, flags, cr);
			if (err)
				error = err;
		} else {
			io_len = PAGESIZE;
		}
	}
out:
	zfs_range_unlock(rl);
	if ((flags & B_ASYNC) == 0 || zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zfsvfs->z_log, zp->z_id);
	ZFS_EXIT(zfsvfs);
	return (error);
}
#endif	/* sun */

/*ARGSUSED*/
int
zfs_inactive(vnode_t *vp)
{
	znode_t	*zp = VTOZ(vp);
	zfsvfs_t *zfsvfs;
	int error;

	if (!zp)
		return 0;

	zfsvfs = zp->z_zfsvfs;

	rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_READER);
	if (zp->z_sa_hdl == NULL) {
		/*
		 * The fs has been unmounted, or we did a
		 * suspend/resume and this file no longer exists.
		 */
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		return 0;
	}

#ifndef __OSV__
	/*
	 * The if clause below is used to speed up the recycle
	 * of a inode which isn't present in the underlying
	 * file system anymore.
	 *
	 * The problem is that OSv VFS layer doesn't support
	 * vrecycle. The call to vrecycle in the test below was also removed.
	 *
	 * vrecycle consists of eliminating all activity associated
	 * with a given unused vnode and putting it back to the list of
	 * free vnode objects.
	 *
	 * Keeping this code alive prevents zfs_inactive from working properly
	 * on unlinked znodes given the facts mentioned above.
	 */
	mutex_enter(&zp->z_lock);
	if (zp->z_unlinked) {
		/*
		 * Fast path to recycle a vnode of a removed file.
		 */
		mutex_exit(&zp->z_lock);
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		return 0;
	}
	mutex_exit(&zp->z_lock);
#endif

	if (zp->z_atime_dirty && zp->z_unlinked == 0) {
		dmu_tx_t *tx = dmu_tx_create(zfsvfs->z_os);

		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, zp);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
		} else {
			mutex_enter(&zp->z_lock);
			(void) sa_update(zp->z_sa_hdl, SA_ZPL_ATIME(zfsvfs),
			    (void *)&zp->z_atime, sizeof (zp->z_atime), tx);
			zp->z_atime_dirty = 0;
			mutex_exit(&zp->z_lock);
			dmu_tx_commit(tx);
		}
	}

	/*
	 * This might want to be moved into a separate VOP_RECLAIM eventually.
	 */
	if (zp->z_sa_hdl == NULL)
		zfs_znode_free(zp);
	else
		zfs_zinactive(zp);
	rw_exit(&zfsvfs->z_teardown_inactive_lock);

	vp->v_data = NULL;
	return 0;
}

/*
 * Bounds-check the seek operation.
 *
 *	IN:	vp	- vnode seeking within
 *		ooff	- old file offset
 *		noffp	- pointer to new file offset
 *
 *	RETURN:	0 if success
 *		EINVAL if new offset invalid
 */
/* ARGSUSED */
static int
zfs_seek(vnode_t *vp, file_t *fp, offset_t ooff, offset_t noffp)
{
	if (vp->v_type == VDIR)
		return (0);
	return ((noffp < 0 || noffp > MAXOFFSET_T) ? EINVAL : 0);
}

#ifdef NOTYET
#ifdef sun
/*
 * Pre-filter the generic locking function to trap attempts to place
 * a mandatory lock on a memory mapped file.
 */
static int
zfs_frlock(vnode_t *vp, int cmd, flock64_t *bfp, int flag, offset_t offset,
    flk_callback_t *flk_cbp, cred_t *cr, caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	/*
	 * We are following the UFS semantics with respect to mapcnt
	 * here: If we see that the file is mapped already, then we will
	 * return an error, but we don't worry about races between this
	 * function and zfs_map().
	 */
	if (zp->z_mapcnt > 0 && MANDMODE(zp->z_mode)) {
		ZFS_EXIT(zfsvfs);
		return (EAGAIN);
	}
	ZFS_EXIT(zfsvfs);
	return (fs_frlock(vp, cmd, bfp, flag, offset, flk_cbp, cr, ct));
}

/*
 * If we can't find a page in the cache, we will create a new page
 * and fill it with file data.  For efficiency, we may try to fill
 * multiple pages at once (klustering) to fill up the supplied page
 * list.  Note that the pages to be filled are held with an exclusive
 * lock to prevent access by other threads while they are being filled.
 */
static int
zfs_fillpage(vnode_t *vp, u_offset_t off, struct seg *seg,
    caddr_t addr, page_t *pl[], size_t plsz, enum seg_rw rw)
{
	znode_t *zp = VTOZ(vp);
	page_t *pp, *cur_pp;
	objset_t *os = zp->z_zfsvfs->z_os;
	u_offset_t io_off, total;
	size_t io_len;
	int err;

	if (plsz == PAGESIZE || zp->z_blksz <= PAGESIZE) {
		/*
		 * We only have a single page, don't bother klustering
		 */
		io_off = off;
		io_len = PAGESIZE;
		pp = page_create_va(vp, io_off, io_len,
		    PG_EXCL | PG_WAIT, seg, addr);
	} else {
		/*
		 * Try to find enough pages to fill the page list
		 */
		pp = pvn_read_kluster(vp, off, seg, addr, &io_off,
		    &io_len, off, plsz, 0);
	}
	if (pp == NULL) {
		/*
		 * The page already exists, nothing to do here.
		 */
		*pl = NULL;
		return (0);
	}

	/*
	 * Fill the pages in the kluster.
	 */
	cur_pp = pp;
	for (total = io_off + io_len; io_off < total; io_off += PAGESIZE) {
		caddr_t va;

		ASSERT3U(io_off, ==, cur_pp->p_offset);
		va = zfs_map_page(cur_pp, S_WRITE);
		err = dmu_read(os, zp->z_id, io_off, PAGESIZE, va,
		    DMU_READ_PREFETCH);
		zfs_unmap_page(cur_pp, va);
		if (err) {
			/* On error, toss the entire kluster */
			pvn_read_done(pp, B_ERROR);
			/* convert checksum errors into IO errors */
			if (err == ECKSUM)
				err = EIO;
			return (err);
		}
		cur_pp = cur_pp->p_next;
	}

	/*
	 * Fill in the page list array from the kluster starting
	 * from the desired offset `off'.
	 * NOTE: the page list will always be null terminated.
	 */
	pvn_plist_init(pp, pl, plsz, off, io_len, rw);
	ASSERT(pl == NULL || (*pl)->p_offset == off);

	return (0);
}

/*
 * Return pointers to the pages for the file region [off, off + len]
 * in the pl array.  If plsz is greater than len, this function may
 * also return page pointers from after the specified region
 * (i.e. the region [off, off + plsz]).  These additional pages are
 * only returned if they are already in the cache, or were created as
 * part of a klustered read.
 *
 *	IN:	vp	- vnode of file to get data from.
 *		off	- position in file to get data from.
 *		len	- amount of data to retrieve.
 *		plsz	- length of provided page list.
 *		seg	- segment to obtain pages for.
 *		addr	- virtual address of fault.
 *		rw	- mode of created pages.
 *		cr	- credentials of caller.
 *		ct	- caller context.
 *
 *	OUT:	protp	- protection mode of created pages.
 *		pl	- list of pages created.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - atime updated
 */
/* ARGSUSED */
static int
zfs_getpage(vnode_t *vp, offset_t off, size_t len, uint_t *protp,
	page_t *pl[], size_t plsz, struct seg *seg, caddr_t addr,
	enum seg_rw rw, cred_t *cr, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	page_t		**pl0 = pl;
	int		err = 0;

	/* we do our own caching, faultahead is unnecessary */
	if (pl == NULL)
		return (0);
	else if (len > plsz)
		len = plsz;
	else
		len = P2ROUNDUP(len, PAGESIZE);
	ASSERT(plsz >= len);

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (protp)
		*protp = PROT_ALL;

	/*
	 * Loop through the requested range [off, off + len) looking
	 * for pages.  If we don't find a page, we will need to create
	 * a new page and fill it with data from the file.
	 */
	while (len > 0) {
		if (*pl = page_lookup(vp, off, SE_SHARED))
			*(pl+1) = NULL;
		else if (err = zfs_fillpage(vp, off, seg, addr, pl, plsz, rw))
			goto out;
		while (*pl) {
			ASSERT3U((*pl)->p_offset, ==, off);
			off += PAGESIZE;
			addr += PAGESIZE;
			if (len > 0) {
				ASSERT3U(len, >=, PAGESIZE);
				len -= PAGESIZE;
			}
			ASSERT3U(plsz, >=, PAGESIZE);
			plsz -= PAGESIZE;
			pl++;
		}
	}

	/*
	 * Fill out the page array with any pages already in the cache.
	 */
	while (plsz > 0 &&
	    (*pl++ = page_lookup_nowait(vp, off, SE_SHARED))) {
			off += PAGESIZE;
			plsz -= PAGESIZE;
	}
out:
	if (err) {
		/*
		 * Release any pages we have previously locked.
		 */
		while (pl > pl0)
			page_unlock(*--pl);
	} else {
		ZFS_ACCESSTIME_STAMP(zfsvfs, zp);
	}

	*pl = NULL;

	ZFS_EXIT(zfsvfs);
	return (err);
}

/*
 * Request a memory map for a section of a file.  This code interacts
 * with common code and the VM system as follows:
 *
 *	common code calls mmap(), which ends up in smmap_common()
 *
 *	this calls VOP_MAP(), which takes you into (say) zfs
 *
 *	zfs_map() calls as_map(), passing segvn_create() as the callback
 *
 *	segvn_create() creates the new segment and calls VOP_ADDMAP()
 *
 *	zfs_addmap() updates z_mapcnt
 */
/*ARGSUSED*/
static int
zfs_map(vnode_t *vp, offset_t off, struct as *as, caddr_t *addrp,
    size_t len, uchar_t prot, uchar_t maxprot, uint_t flags, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	segvn_crargs_t	vn_a;
	int		error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if ((prot & PROT_WRITE) && (zp->z_pflags &
	    (ZFS_IMMUTABLE | ZFS_READONLY | ZFS_APPENDONLY))) {
		ZFS_EXIT(zfsvfs);
		return (EPERM);
	}

	if ((prot & (PROT_READ | PROT_EXEC)) &&
	    (zp->z_pflags & ZFS_AV_QUARANTINED)) {
		ZFS_EXIT(zfsvfs);
		return (EACCES);
	}

	if (vp->v_flag & VNOMAP) {
		ZFS_EXIT(zfsvfs);
		return (ENOSYS);
	}

	if (off < 0 || len > MAXOFFSET_T - off) {
		ZFS_EXIT(zfsvfs);
		return (ENXIO);
	}

	if (vp->v_type != VREG) {
		ZFS_EXIT(zfsvfs);
		return (ENODEV);
	}

	/*
	 * If file is locked, disallow mapping.
	 */
	if (MANDMODE(zp->z_mode) && vn_has_flocks(vp)) {
		ZFS_EXIT(zfsvfs);
		return (EAGAIN);
	}

	as_rangelock(as);
	error = choose_addr(as, addrp, len, off, ADDR_VACALIGN, flags);
	if (error != 0) {
		as_rangeunlock(as);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	vn_a.vp = vp;
	vn_a.offset = (u_offset_t)off;
	vn_a.type = flags & MAP_TYPE;
	vn_a.prot = prot;
	vn_a.maxprot = maxprot;
	vn_a.cred = cr;
	vn_a.amp = NULL;
	vn_a.flags = flags & ~MAP_TYPE;
	vn_a.szc = 0;
	vn_a.lgrp_mem_policy_flags = 0;

	error = as_map(as, *addrp, len, segvn_create, &vn_a);

	as_rangeunlock(as);
	ZFS_EXIT(zfsvfs);
	return (error);
}

/* ARGSUSED */
static int
zfs_addmap(vnode_t *vp, offset_t off, struct as *as, caddr_t addr,
    size_t len, uchar_t prot, uchar_t maxprot, uint_t flags, cred_t *cr,
    caller_context_t *ct)
{
	uint64_t pages = btopr(len);

	atomic_add_64(&VTOZ(vp)->z_mapcnt, pages);
	return (0);
}

/*
 * The reason we push dirty pages as part of zfs_delmap() is so that we get a
 * more accurate mtime for the associated file.  Since we don't have a way of
 * detecting when the data was actually modified, we have to resort to
 * heuristics.  If an explicit msync() is done, then we mark the mtime when the
 * last page is pushed.  The problem occurs when the msync() call is omitted,
 * which by far the most common case:
 *
 * 	open()
 * 	mmap()
 * 	<modify memory>
 * 	munmap()
 * 	close()
 * 	<time lapse>
 * 	putpage() via fsflush
 *
 * If we wait until fsflush to come along, we can have a modification time that
 * is some arbitrary point in the future.  In order to prevent this in the
 * common case, we flush pages whenever a (MAP_SHARED, PROT_WRITE) mapping is
 * torn down.
 */
/* ARGSUSED */
static int
zfs_delmap(vnode_t *vp, offset_t off, struct as *as, caddr_t addr,
    size_t len, uint_t prot, uint_t maxprot, uint_t flags, cred_t *cr,
    caller_context_t *ct)
{
	uint64_t pages = btopr(len);

	ASSERT3U(VTOZ(vp)->z_mapcnt, >=, pages);
	atomic_add_64(&VTOZ(vp)->z_mapcnt, -pages);

	if ((flags & MAP_SHARED) && (prot & PROT_WRITE) &&
	    vn_has_cached_data(vp))
		(void) VOP_PUTPAGE(vp, off, len, B_ASYNC, cr, ct);

	return (0);
}
#endif  /* sun */
#endif  /* NOTYET */

/*
 * convoff - converts the given data (start, whence) to the
 * given whence.
 */
int
convoff(vnode_t *vp, flock64_t *lckdat, int  whence, offset_t offset)
{
	vattr_t vap;
	int error;

	if ((lckdat->l_whence == 2) || (whence == 2)) {
		if ((error = zfs_getattr(vp, &vap) != 0))
			return (error);
	}

	switch (lckdat->l_whence) {
	case 1:
		lckdat->l_start += offset;
		break;
	case 2:
		lckdat->l_start += vap.va_size;
		/* FALLTHRU */
		case 0:
		break;
	default:
		return (EINVAL);
	}

	if (lckdat->l_start < 0)
		return (EINVAL);

	switch (whence) {
	case 1:
		lckdat->l_start -= offset;
		break;
	case 2:
		lckdat->l_start -= vap.va_size;
		/* FALLTHRU */
	case 0:
		break;
	default:
		return (EINVAL);
	}

	lckdat->l_whence = (short)whence;
	return (0);
}

/*
 * Free or allocate space in a file.  Currently, this function only
 * supports the `F_FREESP' command.  However, this command is somewhat
 * misnamed, as its functionality includes the ability to allocate as
 * well as free space.
 *
 *	IN:	vp	- vnode of file to free data in.
 *		cmd	- action to take (F_FREESP and
 * 			  (F_FREESP | F_KEEPSP) supported).
 *		bfp	- section of file to free/alloc.
 *		flag	- current file open mode flags.
 *		offset	- current file offset.
 *		cr	- credentials of caller [UNUSED].
 *		ct	- caller context.
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	vp - ctime|mtime updated
 */
/* ARGSUSED */
static int
zfs_space(vnode_t *vp, int cmd, flock64_t *bfp, int flag,
    offset_t offset)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	uint64_t	off, len, original_size;
	int		error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if (cmd != F_FREESP &&
	    ((cmd & (F_FREESP | F_KEEPSP)) != (F_FREESP | F_KEEPSP))) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	if (error = convoff(vp, bfp, 0, offset)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (bfp->l_len < 0) {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	original_size = zp->z_size;
	off = bfp->l_start;
	len = bfp->l_len; /* 0 means from off to end of file */

	if (error = zfs_freesp(zp, off, len, flag, TRUE)) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	if (cmd & F_KEEPSP) {
		error = zfs_freesp(zp, original_size, 0, 0, TRUE);
	}

	ZFS_EXIT(zfsvfs);
	return (error);
}

static int
zfs_fallocate(vnode_t *vp, int mode, loff_t offset, loff_t len)
{
	int error = EOPNOTSUPP;

	if ((mode & (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE)) ==
	    (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE)) {
		flock64_t bf;

		bf.l_type = F_WRLCK;
		bf.l_whence = 0;
		bf.l_start = offset;
		bf.l_len = len;
		bf.l_pid = 0;

		error = zfs_space(vp, F_FREESP | F_KEEPSP, &bf, FWRITE, offset);
	}

	return (error);
}

#ifdef NOTYET
CTASSERT(sizeof(struct zfid_short) <= sizeof(struct fid));
CTASSERT(sizeof(struct zfid_long) <= sizeof(struct fid));

/*ARGSUSED*/
static int
zfs_fid(vnode_t *vp, fid_t *fidp, caller_context_t *ct)
{
	znode_t		*zp = VTOZ(vp);
	zfsvfs_t	*zfsvfs = zp->z_zfsvfs;
	uint32_t	gen;
	uint64_t	gen64;
	uint64_t	object = zp->z_id;
	zfid_short_t	*zfid;
	int		size, i, error;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_GEN(zfsvfs),
	    &gen64, sizeof (uint64_t))) != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	gen = (uint32_t)gen64;

	size = (zfsvfs->z_parent != zfsvfs) ? LONG_FID_LEN : SHORT_FID_LEN;
	fidp->fid_len = size;

	zfid = (zfid_short_t *)fidp;

	zfid->zf_len = size;

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	/* Must have a non-zero generation number to distinguish from .zfs */
	if (gen == 0)
		gen = 1;
	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = (uint8_t)(gen >> (8 * i));

	if (size == LONG_FID_LEN) {
		uint64_t	objsetid = dmu_objset_id(zfsvfs->z_os);
		zfid_long_t	*zlfid;

		zlfid = (zfid_long_t *)fidp;

		for (i = 0; i < sizeof (zlfid->zf_setid); i++)
			zlfid->zf_setid[i] = (uint8_t)(objsetid >> (8 * i));

		/* XXX - this should be the generation number for the objset */
		for (i = 0; i < sizeof (zlfid->zf_setgen); i++)
			zlfid->zf_setgen[i] = 0;
	}

	ZFS_EXIT(zfsvfs);
	return (0);
}

static int
zfs_pathconf(vnode_t *vp, int cmd, ulong_t *valp, cred_t *cr,
    caller_context_t *ct)
{
	znode_t		*zp, *xzp;
	zfsvfs_t	*zfsvfs;
	zfs_dirlock_t	*dl;
	int		error;

	switch (cmd) {
	case _PC_LINK_MAX:
		*valp = INT_MAX;
		return (0);

	case _PC_FILESIZEBITS:
		*valp = 64;
		return (0);
#ifdef sun
	case _PC_XATTR_EXISTS:
		zp = VTOZ(vp);
		zfsvfs = zp->z_zfsvfs;
		ZFS_ENTER(zfsvfs);
		ZFS_VERIFY_ZP(zp);
		*valp = 0;
		error = zfs_dirent_lock(&dl, zp, "", &xzp,
		    ZXATTR | ZEXISTS | ZSHARED, NULL, NULL);
		if (error == 0) {
			zfs_dirent_unlock(dl);
			if (!zfs_dirempty(xzp))
				*valp = 1;
			VN_RELE(ZTOV(xzp));
		} else if (error == ENOENT) {
			/*
			 * If there aren't extended attributes, it's the
			 * same as having zero of them.
			 */
			error = 0;
		}
		ZFS_EXIT(zfsvfs);
		return (error);

	case _PC_SATTR_ENABLED:
	case _PC_SATTR_EXISTS:
		*valp = vfs_has_feature(vp->v_vfsp, VFSFT_SYSATTR_VIEWS) &&
		    (vp->v_type == VREG || vp->v_type == VDIR);
		return (0);

	case _PC_ACCESS_FILTERING:
		*valp = vfs_has_feature(vp->v_vfsp, VFSFT_ACCESS_FILTER) &&
		    vp->v_type == VDIR;
		return (0);

	case _PC_ACL_ENABLED:
		*valp = _ACL_ACE_ENABLED;
		return (0);
#endif	/* sun */
	case _PC_MIN_HOLE_SIZE:
		*valp = (int)SPA_MINBLOCKSIZE;
		return (0);
#ifdef sun
	case _PC_TIMESTAMP_RESOLUTION:
		/* nanosecond timestamp resolution */
		*valp = 1L;
		return (0);
#endif	/* sun */
	case _PC_ACL_EXTENDED:
		*valp = 0;
		return (0);

	case _PC_ACL_NFS4:
		*valp = 1;
		return (0);

	case _PC_ACL_PATH_MAX:
		*valp = ACL_MAX_ENTRIES;
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

/*ARGSUSED*/
static int
zfs_getsecattr(vnode_t *vp, vsecattr_t *vsecp, int flag, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;
	boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);
	error = zfs_getacl(zp, vsecp, skipaclchk, cr);
	ZFS_EXIT(zfsvfs);

	return (error);
}

/*ARGSUSED*/
static int
zfs_setsecattr(vnode_t *vp, vsecattr_t *vsecp, int flag, cred_t *cr,
    caller_context_t *ct)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error;
	boolean_t skipaclchk = (flag & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	zilog_t	*zilog = zfsvfs->z_log;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	error = zfs_setacl(zp, vsecp, skipaclchk, cr);

	if (zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zilog, 0);

	ZFS_EXIT(zfsvfs);
	return (error);
}

static int
ioflags(int ioflags)
{
	int flags = 0;

	if (ioflags & IO_APPEND)
		flags |= FAPPEND;
	if (ioflags & IO_NDELAY)
        	flags |= FNONBLOCK;
	if (ioflags & IO_SYNC)
		flags |= (FSYNC | FDSYNC | FRSYNC);

	return (flags);
}

static int
zfs_getpages(struct vnode *vp, vm_page_t *m, int count, int reqpage)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	objset_t *os = zp->z_zfsvfs->z_os;
	vm_page_t mreq;
	vm_object_t object;
	caddr_t va;
	struct sf_buf *sf;
	int i, error;
	int pcount, size;

	ZFS_ENTER(zfsvfs);
	ZFS_VERIFY_ZP(zp);

	pcount = round_page(count) / PAGE_SIZE;
	mreq = m[reqpage];
	object = mreq->object;
	error = 0;

	KASSERT(vp->v_object == object, ("mismatching object"));

	VM_OBJECT_LOCK(object);

	for (i = 0; i < pcount; i++) {
		if (i != reqpage) {
			vm_page_lock(m[i]);
			vm_page_free(m[i]);
			vm_page_unlock(m[i]);
		}
	}

	if (mreq->valid) {
		if (mreq->valid != VM_PAGE_BITS_ALL)
			vm_page_zero_invalid(mreq, TRUE);
		VM_OBJECT_UNLOCK(object);
		ZFS_EXIT(zfsvfs);
		return (VM_PAGER_OK);
	}

	PCPU_INC(cnt.v_vnodein);
	PCPU_INC(cnt.v_vnodepgsin);

	if (IDX_TO_OFF(mreq->pindex) >= object->un_pager.vnp.vnp_size) {
		VM_OBJECT_UNLOCK(object);
		ZFS_EXIT(zfsvfs);
		return (VM_PAGER_BAD);
	}

	size = PAGE_SIZE;
	if (IDX_TO_OFF(mreq->pindex) + size > object->un_pager.vnp.vnp_size)
		size = object->un_pager.vnp.vnp_size - IDX_TO_OFF(mreq->pindex);

	VM_OBJECT_UNLOCK(object);
	va = zfs_map_page(mreq, &sf);
	error = dmu_read(os, zp->z_id, IDX_TO_OFF(mreq->pindex),
	    size, va, DMU_READ_PREFETCH);
	if (size != PAGE_SIZE)
		bzero(va + size, PAGE_SIZE - size);
	zfs_unmap_page(sf);
	VM_OBJECT_LOCK(object);

	if (!error)
		mreq->valid = VM_PAGE_BITS_ALL;
	KASSERT(mreq->dirty == 0, ("zfs_getpages: page %p is dirty", mreq));

	VM_OBJECT_UNLOCK(object);

	ZFS_ACCESSTIME_STAMP(zfsvfs, zp);
	ZFS_EXIT(zfsvfs);
	return (error ? VM_PAGER_ERROR : VM_PAGER_OK);
}

/*
 * FreeBSD's extended attributes namespace defines file name prefix for ZFS'
 * extended attribute name:
 *
 *	NAMESPACE	PREFIX	
 *	system		freebsd:system:
 *	user		(none, can be used to access ZFS fsattr(5) attributes
 *			created on Solaris)
 */
static int
zfs_create_attrname(int attrnamespace, const char *name, char *attrname,
    size_t size)
{
	const char *namespace, *prefix, *suffix;

	/* We don't allow '/' character in attribute name. */
	if (strchr(name, '/') != NULL)
		return (EINVAL);
	/* We don't allow attribute names that start with "freebsd:" string. */
	if (strncmp(name, "freebsd:", 8) == 0)
		return (EINVAL);

	bzero(attrname, size);

	switch (attrnamespace) {
	case EXTATTR_NAMESPACE_USER:
#if 0
		prefix = "freebsd:";
		namespace = EXTATTR_NAMESPACE_USER_STRING;
		suffix = ":";
#else
		/*
		 * This is the default namespace by which we can access all
		 * attributes created on Solaris.
		 */
		prefix = namespace = suffix = "";
#endif
		break;
	case EXTATTR_NAMESPACE_SYSTEM:
		prefix = "freebsd:";
		namespace = EXTATTR_NAMESPACE_SYSTEM_STRING;
		suffix = ":";
		break;
	case EXTATTR_NAMESPACE_EMPTY:
	default:
		return (EINVAL);
	}
	if (snprintf(attrname, size, "%s%s%s%s", prefix, namespace, suffix,
	    name) >= size) {
		return (ENAMETOOLONG);
	}
	return (0);
}

/*
 * Vnode operating to retrieve a named extended attribute.
 */
static int
zfs_getextattr(struct vop_getextattr_args *ap)
/*
vop_getextattr {
	IN struct vnode *a_vp;
	IN int a_attrnamespace;
	IN const char *a_name;
	INOUT struct uio *a_uio;
	OUT size_t *a_size;
	IN struct ucred *a_cred;
	IN struct thread *a_td;
};
*/
{
	zfsvfs_t *zfsvfs = VTOZ(ap->a_vp)->z_zfsvfs;
	struct thread *td = ap->a_td;
	struct nameidata nd;
	char attrname[255];
	struct vattr va;
	vnode_t *xvp = NULL, *vp;
	int error, flags;

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VREAD);
	if (error != 0)
		return (error);

	error = zfs_create_attrname(ap->a_attrnamespace, ap->a_name, attrname,
	    sizeof(attrname));
	if (error != 0)
		return (error);

	ZFS_ENTER(zfsvfs);

	error = zfs_lookup(ap->a_vp, NULL, &xvp, NULL, 0, ap->a_cred, td,
	    LOOKUP_XATTR);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	flags = FREAD;
	NDINIT_ATVP(&nd, LOOKUP, NOFOLLOW | MPSAFE, UIO_SYSSPACE, attrname,
	    xvp, td);
	error = vn_open_cred(&nd, &flags, 0, 0, ap->a_cred, NULL);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		if (error == ENOENT)
			error = ENOATTR;
		return (error);
	}

	if (ap->a_size != NULL) {
		error = VOP_GETATTR(vp, &va, ap->a_cred);
		if (error == 0)
			*ap->a_size = (size_t)va.va_size;
	} else if (ap->a_uio != NULL)
		error = VOP_READ(vp, ap->a_uio, IO_UNIT, ap->a_cred);

	VOP_UNLOCK(vp, 0);
	vn_close(vp, flags, ap->a_cred, td);
	ZFS_EXIT(zfsvfs);

	return (error);
}

/*
 * Vnode operation to remove a named attribute.
 */
int
zfs_deleteextattr(struct vop_deleteextattr_args *ap)
/*
vop_deleteextattr {
	IN struct vnode *a_vp;
	IN int a_attrnamespace;
	IN const char *a_name;
	IN struct ucred *a_cred;
	IN struct thread *a_td;
};
*/
{
	zfsvfs_t *zfsvfs = VTOZ(ap->a_vp)->z_zfsvfs;
	struct thread *td = ap->a_td;
	struct nameidata nd;
	char attrname[255];
	struct vattr va;
	vnode_t *xvp = NULL, *vp;
	int error, flags;

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VWRITE);
	if (error != 0)
		return (error);

	error = zfs_create_attrname(ap->a_attrnamespace, ap->a_name, attrname,
	    sizeof(attrname));
	if (error != 0)
		return (error);

	ZFS_ENTER(zfsvfs);

	error = zfs_lookup(ap->a_vp, NULL, &xvp, NULL, 0, ap->a_cred, td,
	    LOOKUP_XATTR);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	NDINIT_ATVP(&nd, DELETE, NOFOLLOW | LOCKPARENT | LOCKLEAF | MPSAFE,
	    UIO_SYSSPACE, attrname, xvp, td);
	error = namei(&nd);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		if (error == ENOENT)
			error = ENOATTR;
		return (error);
	}
	error = VOP_REMOVE(nd.ni_dvp, vp, &nd.ni_cnd);

	vput(nd.ni_dvp);
	if (vp == nd.ni_dvp)
		vrele(vp);
	else
		vput(vp);
	ZFS_EXIT(zfsvfs);

	return (error);
}

/*
 * Vnode operation to set a named attribute.
 */
static int
zfs_setextattr(struct vop_setextattr_args *ap)
/*
vop_setextattr {
	IN struct vnode *a_vp;
	IN int a_attrnamespace;
	IN const char *a_name;
	INOUT struct uio *a_uio;
	IN struct ucred *a_cred;
	IN struct thread *a_td;
};
*/
{
	zfsvfs_t *zfsvfs = VTOZ(ap->a_vp)->z_zfsvfs;
	struct thread *td = ap->a_td;
	struct nameidata nd;
	char attrname[255];
	struct vattr va;
	vnode_t *xvp = NULL, *vp;
	int error, flags;

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VWRITE);
	if (error != 0)
		return (error);

	error = zfs_create_attrname(ap->a_attrnamespace, ap->a_name, attrname,
	    sizeof(attrname));
	if (error != 0)
		return (error);

	ZFS_ENTER(zfsvfs);

	error = zfs_lookup(ap->a_vp, NULL, &xvp, NULL, 0, ap->a_cred, td,
	    LOOKUP_XATTR | CREATE_XATTR_DIR);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	flags = FFLAGS(O_WRONLY | O_CREAT);
	NDINIT_ATVP(&nd, LOOKUP, NOFOLLOW | MPSAFE, UIO_SYSSPACE, attrname,
	    xvp, td);
	error = vn_open_cred(&nd, &flags, 0600, 0, ap->a_cred, NULL);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	VATTR_NULL(&va);
	va.va_size = 0;
	error = VOP_SETATTR(vp, &va, ap->a_cred);
	if (error == 0)
		VOP_WRITE(vp, ap->a_uio, IO_UNIT | IO_SYNC, ap->a_cred);

	VOP_UNLOCK(vp, 0);
	vn_close(vp, flags, ap->a_cred, td);
	ZFS_EXIT(zfsvfs);

	return (error);
}

/*
 * Vnode operation to retrieve extended attributes on a vnode.
 */
static int
zfs_listextattr(struct vop_listextattr_args *ap)
/*
vop_listextattr {
	IN struct vnode *a_vp;
	IN int a_attrnamespace;
	INOUT struct uio *a_uio;
	OUT size_t *a_size;
	IN struct ucred *a_cred;
	IN struct thread *a_td;
};
*/
{
	zfsvfs_t *zfsvfs = VTOZ(ap->a_vp)->z_zfsvfs;
	struct thread *td = ap->a_td;
	struct nameidata nd;
	char attrprefix[16];
	u_char dirbuf[sizeof(struct dirent)];
	struct dirent *dp;
	struct iovec aiov;
	struct uio auio, *uio = ap->a_uio;
	size_t *sizep = ap->a_size;
	size_t plen;
	vnode_t *xvp = NULL, *vp;
	int done, error, eof, pos;

	error = extattr_check_cred(ap->a_vp, ap->a_attrnamespace,
	    ap->a_cred, ap->a_td, VREAD);
	if (error != 0)
		return (error);

	error = zfs_create_attrname(ap->a_attrnamespace, "", attrprefix,
	    sizeof(attrprefix));
	if (error != 0)
		return (error);
	plen = strlen(attrprefix);

	ZFS_ENTER(zfsvfs);

	if (sizep != NULL)
		*sizep = 0;

	error = zfs_lookup(ap->a_vp, NULL, &xvp, NULL, 0, ap->a_cred, td,
	    LOOKUP_XATTR);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		/*
		 * ENOATTR means that the EA directory does not yet exist,
		 * i.e. there are no extended attributes there.
		 */
		if (error == ENOATTR)
			error = 0;
		return (error);
	}

	NDINIT_ATVP(&nd, LOOKUP, NOFOLLOW | LOCKLEAF | LOCKSHARED | MPSAFE,
	    UIO_SYSSPACE, ".", xvp, td);
	error = namei(&nd);
	vp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (error != 0) {
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = td;
	auio.uio_rw = UIO_READ;
	auio.uio_offset = 0;

	do {
		u_char nlen;

		aiov.iov_base = (void *)dirbuf;
		aiov.iov_len = sizeof(dirbuf);
		auio.uio_resid = sizeof(dirbuf);
		error = VOP_READDIR(vp, &auio, ap->a_cred, &eof, NULL, NULL);
		done = sizeof(dirbuf) - auio.uio_resid;
		if (error != 0)
			break;
		for (pos = 0; pos < done;) {
			dp = (struct dirent *)(dirbuf + pos);
			pos += dp->d_reclen;
			/*
			 * XXX: Temporarily we also accept DT_UNKNOWN, as this
			 * is what we get when attribute was created on Solaris.
			 */
			if (dp->d_type != DT_REG && dp->d_type != DT_UNKNOWN)
				continue;
			if (plen == 0 && strncmp(dp->d_name, "freebsd:", 8) == 0)
				continue;
			else if (strncmp(dp->d_name, attrprefix, plen) != 0)
				continue;
			nlen = dp->d_namlen - plen;
			if (sizep != NULL)
				*sizep += 1 + nlen;
			else if (uio != NULL) {
				/*
				 * Format of extattr name entry is one byte for
				 * length and the rest for name.
				 */
				error = uiomove(&nlen, 1, uio->uio_rw, uio);
				if (error == 0) {
					error = uiomove(dp->d_name + plen, nlen,
					    uio->uio_rw, uio);
				}
				if (error != 0)
					break;
			}
		}
	} while (!eof && error == 0);

	vput(vp);
	ZFS_EXIT(zfsvfs);

	return (error);
}
#endif /* NOTYET */

struct vnops zfs_vnops = {
	zfs_open,			/* open */
	zfs_close,			/* close */
	zfs_read,			/* read */
	zfs_write,			/* write */
	zfs_seek,			/* seek */
	zfs_ioctl,			/* ioctl */
	zfs_fsync,			/* fsync */
	zfs_readdir,			/* readdir */
	zfs_lookup,			/* lookup */
	zfs_create,			/* create */
	zfs_remove,			/* remove */
	zfs_rename,			/* rename */
	zfs_mkdir,			/* mkdir */
	zfs_rmdir,			/* rmdir */
	zfs_getattr,			/* getattr */
	zfs_setattr,			/* setattr */
	zfs_inactive,			/* inactive */
	zfs_truncate,			/* truncate */
	zfs_link,			/* link */
	zfs_arc,			/* arc */
	zfs_fallocate,			/* fallocate */
	zfs_readlink,			/* read link */
	zfs_symlink,			/* symbolic link */
};
