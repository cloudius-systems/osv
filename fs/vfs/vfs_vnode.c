/*
 * Copyright (c) 2005-2008, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * vfs_vnode.c - vnode service
 */

#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#include <osv/prex.h>
#include <osv/vnode.h>
#include "vfs.h"

enum vtype iftovt_tab[16] = {
	VNON, VFIFO, VCHR, VNON, VDIR, VNON, VBLK, VNON,
	VREG, VNON, VLNK, VNON, VSOCK, VNON, VNON, VBAD,
};
int vttoif_tab[10] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK,
	S_IFSOCK, S_IFIFO, S_IFMT, S_IFMT
};

/*
 * Memo:
 *
 * Function   Ref count Lock
 * ---------- --------- ----------
 * vn_lock     *        Lock
 * vn_unlock   *        Unlock
 * vget        1        Lock
 * vput       -1        Unlock
 * vref       +1        *
 * vrele      -1        *
 */

#define VNODE_BUCKETS 32		/* size of vnode hash table */

/*
 * vnode table.
 * All active (opened) vnodes are stored on this hash table.
 * They can be accessed by its path name.
 */
static LIST_HEAD(vnode_hash_head, vnode) vnode_table[VNODE_BUCKETS];

/*
 * Global lock to access all vnodes and vnode table.
 * If a vnode is already locked, there is no need to
 * lock this global lock to access internal data.
 */
static mutex_t vnode_lock = MUTEX_INITIALIZER;
#define VNODE_LOCK()	mutex_lock(&vnode_lock)
#define VNODE_UNLOCK()	mutex_unlock(&vnode_lock)
#define VNODE_OWNED()	mutex_owned(&vnode_lock)

/*
 * Get the hash value from the mount point and path name.
 * XXX(hch): replace with a better hash for 64-bit pointers.
 */
static u_int
vn_hash(struct mount *mp, uint64_t ino)
{
	return (ino ^ (unsigned long)mp) & (VNODE_BUCKETS - 1);
}

/*
 * Returns locked vnode for specified mount point and path.
 * vn_lock() will increment the reference count of vnode.
 *
 * Locking: VNODE_LOCK must be held.
 */
struct vnode *
vn_lookup(struct mount *mp, uint64_t ino)
{
	struct vnode *vp;

	assert(VNODE_OWNED());
	LIST_FOREACH(vp, &vnode_table[vn_hash(mp, ino)], v_link) {
		if (vp->v_mount == mp && vp->v_ino == ino) {
			vp->v_refcnt++;
			mutex_lock(&vp->v_lock);
			vp->v_nrlocks++;
			return vp;
		}
	}
	return NULL;		/* not found */
}

/*
 * Lock vnode
 */
void
vn_lock(struct vnode *vp)
{
	ASSERT(vp);
	ASSERT(vp->v_refcnt > 0);

	mutex_lock(&vp->v_lock);
	vp->v_nrlocks++;
	DPRINTF(VFSDB_VNODE, ("vn_lock:   %s\n", vp->v_path));
}

/*
 * Unlock vnode
 */
void
vn_unlock(struct vnode *vp)
{
	ASSERT(vp);
	ASSERT(vp->v_refcnt > 0);
	ASSERT(vp->v_nrlocks > 0);

	DPRINTF(VFSDB_VNODE, ("vn_unlock: %s\n", vp->v_path));
	vp->v_nrlocks--;
	mutex_unlock(&vp->v_lock);
}

/*
 * Allocate new vnode for specified path.
 * Increment its reference count and lock it.
 * Returns 1 if vnode was found in cache; otherwise returns 0.
 */
int
vget(struct mount *mp, uint64_t ino, struct vnode **vpp)
{
	struct vnode *vp;
	int error;

	*vpp = NULL;

	DPRINTF(VFSDB_VNODE, ("vget %LLu\n", ino));

	VNODE_LOCK();

	vp = vn_lookup(mp, ino);
	if (vp) {
		VNODE_UNLOCK();
		*vpp = vp;
		return 1;
	}

	if (!(vp = malloc(sizeof(struct vnode)))) {
		VNODE_UNLOCK();
		return 0;
	}

	memset(vp, 0, sizeof(struct vnode));

	LIST_INIT(&vp->v_names);
	vp->v_ino = ino;
	vp->v_mount = mp;
	vp->v_refcnt = 1;
	vp->v_op = mp->m_op->vfs_vnops;
	mutex_init(&vp->v_lock);
	vp->v_nrlocks = 0;

	/*
	 * Request to allocate fs specific data for vnode.
	 */
	if ((error = VFS_VGET(mp, vp)) != 0) {
		VNODE_UNLOCK();
		mutex_destroy(&vp->v_lock);
		free(vp);
		return NULL;
	}
	vfs_busy(vp->v_mount);
	mutex_lock(&vp->v_lock);
	vp->v_nrlocks++;

	LIST_INSERT_HEAD(&vnode_table[vn_hash(mp, ino)], vp, v_link);
	VNODE_UNLOCK();

	*vpp = vp;

	return 0;
}

/*
 * Unlock vnode and decrement its reference count.
 */
void
vput(struct vnode *vp)
{
	ASSERT(vp);
	ASSERT(vp->v_nrlocks > 0);
	ASSERT(vp->v_refcnt > 0);
	DPRINTF(VFSDB_VNODE, ("vput: ref=%d %s\n", vp->v_refcnt,
			      vp->v_path));

	VNODE_LOCK();
	vp->v_refcnt--;
	if (vp->v_refcnt > 0) {
	    VNODE_UNLOCK();
		vn_unlock(vp);
		return;
	}
	LIST_REMOVE(vp, v_link);
	VNODE_UNLOCK();

	/*
	 * Deallocate fs specific vnode data
	 */
	if (vp->v_op->vop_inactive)
		VOP_INACTIVE(vp);
	vfs_unbusy(vp->v_mount);
	vp->v_nrlocks--;
	ASSERT(vp->v_nrlocks == 0);
	mutex_unlock(&vp->v_lock);
	mutex_destroy(&vp->v_lock);
	free(vp);
}

/*
 * Increment the reference count on an active vnode.
 */
void
vref(struct vnode *vp)
{
	ASSERT(vp);
	ASSERT(vp->v_refcnt > 0);	/* Need vget */

	VNODE_LOCK();
	DPRINTF(VFSDB_VNODE, ("vref: ref=%d\n", vp->v_refcnt));
	vp->v_refcnt++;
	VNODE_UNLOCK();
}

/*
 * Decrement the reference count of the vnode.
 * Any code in the system which is using vnode should call vrele()
 * when it is finished with the vnode.
 * If count drops to zero, call inactive routine and return to freelist.
 */
void
vrele(struct vnode *vp)
{
	ASSERT(vp);
	ASSERT(vp->v_refcnt > 0);

	VNODE_LOCK();
	DPRINTF(VFSDB_VNODE, ("vrele: ref=%d\n", vp->v_refcnt));
	vp->v_refcnt--;
	if (vp->v_refcnt > 0) {
		VNODE_UNLOCK();
		return;
	}
	LIST_REMOVE(vp, v_link);
	VNODE_UNLOCK();

	/*
	 * Deallocate fs specific vnode data
	 */
	VOP_INACTIVE(vp);
	vfs_unbusy(vp->v_mount);
	mutex_destroy(&vp->v_lock);
	free(vp);
}

/*
 * Return reference count.
 */
int
vcount(struct vnode *vp)
{
	int count;

	vn_lock(vp);
	count = vp->v_refcnt;
	vn_unlock(vp);
	return count;
}

/*
 * Remove all vnode in the vnode table for unmount.
 */
void
vflush(struct mount *mp)
{
}

int
vn_stat(struct vnode *vp, struct stat *st)
{
	struct vattr vattr;
	struct vattr *vap;
	mode_t mode;
	int error;

	vap = &vattr;

	memset(st, 0, sizeof(struct stat));

	memset(vap, 0, sizeof(struct vattr));

	error = VOP_GETATTR(vp, vap);
	if (error)
		return error;

	st->st_ino = (ino_t)vap->va_nodeid;
	st->st_size = vp->v_size;
	mode = vp->v_mode;
	switch (vp->v_type) {
	case VREG:
		mode |= S_IFREG;
		break;
	case VDIR:
		mode |= S_IFDIR;
		break;
	case VBLK:
		mode |= S_IFBLK;
		break;
	case VCHR:
		mode |= S_IFCHR;
		break;
	case VLNK:
		mode |= S_IFLNK;
		break;
	case VSOCK:
		mode |= S_IFSOCK;
		break;
	case VFIFO:
		mode |= S_IFIFO;
		break;
	default:
		return EBADF;
	};
	st->st_mode = mode;
	st->st_nlink = vap->va_nlink;
	st->st_blksize = BSIZE;
	st->st_blocks = vp->v_size / S_BLKSIZE;
	st->st_uid = vap->va_uid;
	st->st_gid = vap->va_gid;
	st->st_dev = vap->va_fsid;
	if (vp->v_type == VCHR || vp->v_type == VBLK)
		st->st_rdev = vap->va_rdev;

	st->st_atim = vap->va_atime;
	st->st_mtim = vap->va_mtime;
	st->st_ctim = vap->va_ctime;

	return 0;
}

/*
 * Set access and modification times of the vnode
 */
int
vn_settimes(struct vnode *vp, struct timespec times[2])
{
    struct vattr vattr;
    struct vattr *vap;
    int error;

    vap = &vattr;
    memset(vap, 0, sizeof(struct vattr));

    vap->va_atime = times[0];
    vap->va_mtime = times[1];
    vap->va_mask = AT_ATIME | AT_MTIME;

    vn_lock(vp);
    error = VOP_SETATTR(vp, vap);
    vn_unlock(vp);

    return error;
}

/*
 * Check permission on vnode pointer.
 */
int
vn_access(struct vnode *vp, int flags)
{
	int error = 0;

	if ((flags & VEXEC) && (vp->v_mode & 0111) == 0) {
		error = EACCES;
		goto out;
	}
	if ((flags & VREAD) && (vp->v_mode & 0444) == 0) {
		error = EACCES;
		goto out;
	}
	if (flags & VWRITE) {
		if (vp->v_mount->m_flags & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		if ((vp->v_mode & 0222) == 0) {
			error = EACCES;
			goto out;
		}
	}
 out:
	return error;
}

#ifdef DEBUG_VFS
/*
 * Dump all all vnode.
 */
void
vnode_dump(void)
{
	int i;
	struct vnode *vp;
	struct mount *mp;
	char type[][6] = { "VNON ", "VREG ", "VDIR ", "VBLK ", "VCHR ",
			   "VLNK ", "VSOCK", "VFIFO" };

	VNODE_LOCK();
	dprintf("Dump vnode\n");
	dprintf(" vnode    mount    type  refcnt blkno    path\n");
	dprintf(" -------- -------- ----- ------ -------- ------------------------------\n");

	for (i = 0; i < VNODE_BUCKETS; i++) {
	        LIST_FOREACH(vp, &vnode_table[i], v_link) {
			mp = vp->v_mount;

			dprintf(" %08x %08x %s %6d %8d %s%s\n", (u_int)vp,
				(u_int)mp, type[vp->v_type], vp->v_refcnt,
				(strlen(mp->m_path) == 1) ? "\0" : mp->m_path,
				vp->v_path);
		}
	}
	dprintf("\n");
	VNODE_UNLOCK();
}
#endif

int
vop_nullop(void)
{

	return 0;
}

int
vop_einval(void)
{

	return EINVAL;
}

int
vop_eperm(void)
{

	return EPERM;
}

/*
 * vnode_init() is called once (from vfs_init)
 * in initialization.
 */
void
vnode_init(void)
{
	int i;

	for (i = 0; i < VNODE_BUCKETS; i++)
		LIST_INIT(&vnode_table[i]);
}

void vn_add_name(struct vnode *vp, struct dentry *dp)
{
	vn_lock(vp);
	LIST_INSERT_HEAD(&vp->v_names, dp, d_names_link);
	vn_unlock(vp);
}

void vn_del_name(struct vnode *vp, struct dentry *dp)
{
	vn_lock(vp);
	LIST_REMOVE(dp, d_names_link);
	vn_unlock(vp);
}

