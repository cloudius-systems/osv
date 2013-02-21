
#include <osv/vnode.h>
#include <osv/file.h>
#include <osv/mount.h>
#include <osv/buf.h>
#include <osv/debug.h>

#include <sys/stat.h>
#include <sys/param.h>

#include <stdio.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include "romfs.h"

static mutex_t romfs_lock = MUTEX_INITIALIZER;

static enum vtype romfs_types[] = {
	[ROMFH_DIR]	= VDIR,
	[ROMFH_REG]	= VREG,
	[ROMFH_SYM]	= VLNK,
	[ROMFH_BLK]	= VBLK,
	[ROMFH_CHR]	= VCHR,
	[ROMFH_SCK]	= VSOCK,
	[ROMFH_FIF]	= VFIFO,
};

static int romfs_diread(struct mount *mp, off_t pos, struct romfs_dinode **dipp,
		struct buf **bpp, unsigned int *offsetp)
{
	unsigned int offset;
	int ret;

	ret = bread(mp->m_dev, pos / BSIZE, bpp);
	if (ret) {
		kprintf("romfs: failed to read inode\n");
		return ret;
	}

	offset = pos % BSIZE;
	*dipp = (*bpp)->b_data + offset;
	if (offsetp)
		*offsetp = offset;
	return 0;
}

/* first entry in a directory */
static inline int romfs_first_entry(struct vnode *dvp, off_t *pos)
{
	struct romfs_node *dnp = dvp->v_data;
	struct romfs_dinode *dip;
	struct buf *bp;
	int ret;

	ret = romfs_diread(dvp->v_mount, dnp->rn_metadata_offset,
			   &dip, &bp, NULL);
	if (!ret) {
		*pos = ntohl(dip->spec) & ROMFH_MASK;
		brelse(bp);
	}

	return ret;
}

/* next inode following this one */
static inline off_t romfs_next(struct romfs_dinode *dip)
{
	return ntohl(dip->next) & ROMFH_MASK;
}

/* check if the offset points to a valid inode */
static inline bool romfs_valid(struct romfs_mount *rmp, off_t pos)
{
	return pos && pos < rmp->rm_maxsize;
}

static int romfs_namelen(struct buf **bpp, unsigned offset,
		size_t *namelen)
{
	struct buf *bp = *bpp;
	int ret;
	void *p;

	*namelen = 0;
	offset += ROMFH_SIZE;

	do {
		daddr_t blkno;

		p = memchr(bp->b_data + offset, 0, BSIZE - offset);
		if (p) {
			*namelen += (p - bp->b_data) - offset;
			*bpp = bp;
			return 0;
		}

		*namelen += (BSIZE - offset);
		offset = 0;
		blkno = bp->b_blkno + 1;
		brelse(bp);
		ret = bread(bp->b_dev, blkno, &bp);
	} while (!ret);

	return ret;
}

static int romfs_read_name(char *name, struct buf **bpp, unsigned int offset)
{
	struct buf *bp = *bpp;
	unsigned namelen;
	int ret;
	void *p;

	offset += ROMFH_SIZE;

	do {
		daddr_t blkno;

		p = memchr(bp->b_data + offset, 0, BSIZE - offset);
		if (p) {
			/* include the trailing \0 */
			namelen = (p - bp->b_data) - offset + 1;
			memcpy(name, bp->b_data + offset, namelen);
			*bpp = bp;
			return 0;
		}

		namelen = BSIZE - offset;
		memcpy(name, bp->b_data + offset, namelen);

		offset = 0;
		name += namelen;
		blkno = bp->b_blkno + 1;
		brelse(bp);
		ret = bread(bp->b_dev, blkno, &bp);
	} while (!ret);

	return ret;
}

static int romfs_name_match(char *name, size_t len, struct buf **bpp,
		unsigned int offset, bool *found)
{
	struct buf *bp = *bpp;
	int ret;

	offset += ROMFH_SIZE;

	do {
		unsigned namelen = BSIZE - offset;
		daddr_t blkno;

		if (len < namelen)
			namelen = len;

		if (memcmp(bp->b_data + offset, name, namelen) != 0) {
			*found = false;
			return 0;
		}

		offset = 0;
		name += namelen;
		len -= namelen;


		if (len == 0) {
			*found = true;
			return 0;
		}

		blkno = bp->b_blkno + 1;
		brelse(bp);

		ret = bread(bp->b_dev, blkno, &bp);
	} while (!ret);

	return ret;
}


int romfs_read_node(struct vnode *vp, unsigned long pos)
{
	struct mount *mp = vp->v_mount;
	struct romfs_node *np = vp->v_data;
	unsigned int offset;
	struct romfs_dinode *dip;
	struct buf *bp;
	size_t namelen = 0;
	unsigned metasize;
	unsigned type;
	int ret;

again:
	ret = romfs_diread(mp, pos,  &dip, &bp, &offset);

	/* walk the chain if this is a hardlink entry */
	type = ntohl(dip->next) & ROMFH_TYPE;
	if (type == ROMFH_HRD) {
		pos = ntohl(dip->spec) & ROMFH_MASK;
		goto again;
	}

	ret = romfs_namelen(&bp, offset, &namelen);
	if (ret)
		return ret;

	metasize = (ROMFH_SIZE + namelen + 1 + ROMFH_PAD) & ROMFH_MASK;

	np->rn_metadata_offset = pos;
	np->rn_data_offset = pos + metasize;

	vp->v_mode = ALLPERMS;
	vp->v_type = romfs_types[type];
	vp->v_size = type == ROMFH_DIR ? metasize : ntohl(dip->size);

	brelse(bp);
	return 0;
}

static int
romfs_lookup(struct vnode *dvp, char *name, struct vnode *vp)
{
	struct mount *mp = dvp->v_mount;
	struct romfs_mount *rmp = mp->m_data;
	size_t len = strlen(name);
	struct romfs_dinode *dip;
	struct buf *bp;
	off_t pos, next_pos;
	unsigned int offset;
	int ret;

	if (*name == '\0')
		return ENOENT;
	if (len > ROMFS_MAXFN)
		return ENAMETOOLONG;


	mutex_lock(&romfs_lock);
	ret = romfs_first_entry(dvp, &pos);
	if (ret)
		goto out_unlock;

	do {
		bool found = false;

		ret = romfs_diread(mp, pos, &dip, &bp, &offset);
		if (ret)
			goto out_unlock;

		next_pos = romfs_next(dip);

		ret = romfs_name_match(name, len, &bp, offset, &found);
		if (ret)
			goto out_unlock;
		brelse(bp);

		if (found) {
			ret = romfs_read_node(vp, pos);
			goto out_unlock;
		}

		pos = next_pos;
	} while (romfs_valid(rmp, pos));

	ret = ENOENT;
out_unlock:
	mutex_unlock(&romfs_lock);
	return ret;
}

static int
romfs_mkdir(vnode_t dvp, char *name, mode_t mode)
{
	if (!S_ISDIR(mode))
		return EINVAL;
	return EPERM;
}

static int
romfs_rmdir(vnode_t dvp, vnode_t vp, char *name)
{
	return EPERM;
}

static int
romfs_remove(vnode_t dvp, vnode_t vp, char *name)
{
	return EPERM;
}

static int
romfs_inactive(vnode_t vp)
{
	free(vp->v_data);
	return 0;
}


static int
romfs_truncate(vnode_t vp, off_t length)
{
	return EROFS;
}

static int
romfs_create(vnode_t dvp, char *name, mode_t mode)
{
	return EPERM;
}

static int
romfs_rename(vnode_t dvp1, vnode_t vp1, char *name1,
	     vnode_t dvp2, vnode_t vp2, char *name2)
{
	return EPERM;
}

static int
romfs_readdir(struct vnode *dvp, struct file *fp, struct dirent *dir)
{
	struct mount *mp = dvp->v_mount;
	struct romfs_mount *rmp = mp->m_data;
	struct romfs_dinode *dip;
	struct buf *bp;
	unsigned int offset;
	off_t pos;
	int ret;

	mutex_lock(&romfs_lock);
	if (fp->f_offset) {
		pos = fp->f_offset;
	} else {
		ret = romfs_first_entry(dvp, &pos);
		if (ret)
			goto out_unlock;
	}

	ret = romfs_diread(mp, pos, &dip, &bp, &offset);
	if (ret)
		goto out_unlock;

	fp->f_offset = romfs_next(dip);
	if (!romfs_valid(rmp, fp->f_offset)) {
		fp->f_offset = rmp->rm_maxsize;
		return ENOENT;
	}

	if ((ntohl(dip->next) & ROMFH_TYPE) == ROMFH_HRD)
		dir->d_fileno = ntohl(dip->spec);
	else
		dir->d_fileno = pos;
	dir->d_type = DT_REG;	// XXX

	ret = romfs_read_name(dir->d_name, &bp, offset);
	if (ret)
		goto out_unlock;

//	dir->d_namelen = strlen(dir->d_name);

	brelse(bp);

	ret = 0;
out_unlock:
	mutex_unlock(&romfs_lock);
	return ret;
}

static int
romfs_read(struct vnode *vp, struct uio *uio, int ioflag)
{
	struct romfs_node *np = vp->v_data;
	size_t len;
	int ret;

	if (vp->v_type == VDIR)
		return EISDIR;
	if (vp->v_type != VREG)
		return EINVAL;
	if (uio->uio_offset < 0)
		return EINVAL;

	if (uio->uio_resid == 0)
		return 0;

	if (uio->uio_offset >= (off_t)vp->v_size)
		return 0;

	if (vp->v_size - uio->uio_offset < uio->uio_resid)
		len = vp->v_size - uio->uio_offset;
	else
		len = uio->uio_resid;

	while (len > 0) {
		off_t pos = np->rn_data_offset + uio->uio_offset;
		unsigned int offset = pos % BSIZE;
		int copy_len = BSIZE - offset;
		daddr_t bno = pos / BSIZE;
		struct buf *bp;

		if (len < copy_len)
			copy_len = len;

		ret = bread(vp->v_mount->m_dev, bno, &bp);
		if (ret)
			break;

		ret = uiomove(bp->b_data + offset, copy_len, uio);
		brelse(bp);

		if (ret)
			break;

		len -= copy_len;
	}

	return ret;
}

#define romfs_open	((vnop_open_t)vop_nullop)
#define romfs_close	((vnop_close_t)vop_nullop)
#define romfs_seek	((vnop_seek_t)vop_nullop)
#define romfs_ioctl	((vnop_ioctl_t)vop_einval)
#define romfs_write	((vnop_write_t)vop_einval)
#define romfs_fsync	((vnop_fsync_t)vop_nullop)
#define romfs_getattr	((vnop_getattr_t)vop_nullop)
#define romfs_setattr	((vnop_setattr_t)vop_nullop)

struct vnops romfs_vnops = {
	.vop_open		= romfs_open,
	.vop_close		= romfs_close,
	.vop_read		= romfs_read,
	.vop_write		= romfs_write,
	.vop_seek		= romfs_seek,
	.vop_ioctl		= romfs_ioctl,
	.vop_fsync		= romfs_fsync,
	.vop_readdir		= romfs_readdir,
	.vop_lookup		= romfs_lookup,
	.vop_create		= romfs_create,
	.vop_remove		= romfs_remove,
	.vop_rename		= romfs_rename,
	.vop_mkdir		= romfs_mkdir,
	.vop_rmdir		= romfs_rmdir,
	.vop_getattr		= romfs_getattr,
	.vop_setattr		= romfs_setattr,
	.vop_inactive		= romfs_inactive,
	.vop_truncate		= romfs_truncate,
};

int
romfs_init(void)
{
	return 0;
}
