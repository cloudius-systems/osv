/*
 * Copyright (c) 2006-2007, Kohsuke Ohtani
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
 * rmafs_vnops.c - vnode operations for RAM file system.
 */

#include <sys/stat.h>
#include <dirent.h>
#include <sys/param.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/file.h>
#include <osv/mount.h>

#include "ramfs.h"


static mutex_t ramfs_lock = MUTEX_INITIALIZER;
static uint64_t inode_count = 1; /* inode 0 is reserved to root */

struct ramfs_node *
ramfs_allocate_node(char *name, int type)
{
	struct ramfs_node *np;

	np = (ramfs_node*)malloc(sizeof(struct ramfs_node));
	if (np == NULL)
		return NULL;
	memset(np, 0, sizeof(struct ramfs_node));

	np->rn_namelen = strlen(name);
	np->rn_name = (char*)malloc(np->rn_namelen + 1);
	if (np->rn_name == NULL) {
		free(np);
		return NULL;
	}
	strlcpy(np->rn_name, name, np->rn_namelen + 1);
	np->rn_type = type;
	return np;
}

void
ramfs_free_node(struct ramfs_node *np)
{
	if (np->rn_buf != NULL)
		free(np->rn_buf);

	free(np->rn_name);
	free(np);
}

static struct ramfs_node *
ramfs_add_node(struct ramfs_node *dnp, char *name, int type)
{
	struct ramfs_node *np, *prev;

	np = ramfs_allocate_node(name, type);
	if (np == NULL)
		return NULL;

	mutex_lock(&ramfs_lock);

	/* Link to the directory list */
	if (dnp->rn_child == NULL) {
		dnp->rn_child = np;
	} else {
		prev = dnp->rn_child;
		while (prev->rn_next != NULL)
			prev = prev->rn_next;
		prev->rn_next = np;
	}
	mutex_unlock(&ramfs_lock);
	return np;
}

static int
ramfs_remove_node(struct ramfs_node *dnp, struct ramfs_node *np)
{
	struct ramfs_node *prev;

	if (dnp->rn_child == NULL)
		return EBUSY;

	mutex_lock(&ramfs_lock);

	/* Unlink from the directory list */
	if (dnp->rn_child == np) {
		dnp->rn_child = np->rn_next;
	} else {
		for (prev = dnp->rn_child; prev->rn_next != np;
		     prev = prev->rn_next) {
			if (prev->rn_next == NULL) {
				mutex_unlock(&ramfs_lock);
				return ENOENT;
			}
		}
		prev->rn_next = np->rn_next;
	}
	ramfs_free_node(np);

	mutex_unlock(&ramfs_lock);
	return 0;
}

static int
ramfs_rename_node(struct ramfs_node *np, char *name)
{
	size_t len;
	char *tmp;

	len = strlen(name);
	if (len <= np->rn_namelen) {
		/* Reuse current name buffer */
		strlcpy(np->rn_name, name, np->rn_namelen + 1);
	} else {
		/* Expand name buffer */
		tmp = (char*)malloc(len + 1);
		if (tmp == NULL)
			return ENOMEM;
		strlcpy(tmp, name, len + 1);
		free(np->rn_name);
		np->rn_name = tmp;
	}
	np->rn_namelen = len;
	return 0;
}

static int
ramfs_lookup(struct vnode *dvp, char *name, struct vnode **vpp)
{
	struct ramfs_node *np, *dnp;
	struct vnode *vp;
	size_t len;
	int found;

	*vpp = NULL;

	if (*name == '\0')
		return ENOENT;

	mutex_lock(&ramfs_lock);

	len = strlen(name);
	dnp = (ramfs_node*)dvp->v_data;
	found = 0;
	for (np = dnp->rn_child; np != NULL; np = np->rn_next) {
		if (np->rn_namelen == len &&
		    memcmp(name, np->rn_name, len) == 0) {
			found = 1;
			break;
		}
	}
	if (found == 0) {
		mutex_unlock(&ramfs_lock);
		return ENOENT;
	}
	if (vget(dvp->v_mount, inode_count++, &vp)) {
		/* found in cache */
		*vpp = vp;
		mutex_unlock(&ramfs_lock);
		return 0;
	}
	if (!vp) {
		mutex_unlock(&ramfs_lock);
		return ENOMEM;
	}
	vp->v_data = np;
	vp->v_mode = ALLPERMS;
	vp->v_type = np->rn_type;
	vp->v_size = np->rn_size;

	mutex_unlock(&ramfs_lock);

	*vpp = vp;

	return 0;
}

static int
ramfs_mkdir(struct vnode *dvp, char *name, mode_t mode)
{
	struct ramfs_node *np;

	DPRINTF(("mkdir %s\n", name));
	if (!S_ISDIR(mode))
		return EINVAL;

	np = (ramfs_node*)ramfs_add_node((ramfs_node*)dvp->v_data, name, VDIR);
	if (np == NULL)
		return ENOMEM;
	np->rn_size = 0;
	return 0;
}

/* Remove a directory */
static int
ramfs_rmdir(struct vnode *dvp, struct vnode *vp, char *name)
{

	return ramfs_remove_node((ramfs_node*)dvp->v_data, (ramfs_node*)vp->v_data);
}

/* Remove a file */
static int
ramfs_remove(struct vnode *dvp, struct vnode *vp, char *name)
{
	DPRINTF(("remove %s in %s\n", name, dvp->v_path));
	return ramfs_remove_node((ramfs_node*)dvp->v_data, (ramfs_node*)vp->v_data);
}

/* Truncate file */
static int
ramfs_truncate(struct vnode *vp, off_t length)
{
	struct ramfs_node *np;
	void *new_buf;
	size_t new_size;

	DPRINTF(("truncate %s length=%d\n", vp->v_path, length));
	np = (ramfs_node*)vp->v_data;

	if (length == 0) {
		if (np->rn_buf != NULL) {
			free(np->rn_buf);
			np->rn_buf = NULL;
			np->rn_bufsize = 0;
		}
	} else if (size_t(length) > np->rn_bufsize) {
		// XXX: this could use a page level allocator
		new_size = round_page(length);
		new_buf = malloc(new_size);
		if (!new_buf)
			return EIO;
		if (np->rn_size != 0) {
			memcpy(new_buf, np->rn_buf, vp->v_size);
			free(np->rn_buf);
		}
		np->rn_buf = (char*)new_buf;
		np->rn_bufsize = new_size;
	}
	np->rn_size = length;
	vp->v_size = length;
	return 0;
}

/*
 * Create empty file.
 */
static int
ramfs_create(struct vnode *dvp, char *name, mode_t mode)
{
	struct ramfs_node *np;

	DPRINTF(("create %s in %s\n", name, dvp->v_path));
	if (!S_ISREG(mode))
		return EINVAL;

	np = ramfs_add_node((ramfs_node*)dvp->v_data, name, VREG);
	if (np == NULL)
		return ENOMEM;
	return 0;
}

static int
ramfs_read(struct vnode *vp, struct file *fp, struct uio *uio, int ioflag)
{
	struct ramfs_node *np = (ramfs_node*)vp->v_data;
	size_t len;

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

	return uiomove(np->rn_buf + uio->uio_offset, len, uio);
}

static int
ramfs_write(struct vnode *vp, struct uio *uio, int ioflag)
{
	struct ramfs_node *np = (ramfs_node*)vp->v_data;

	if (vp->v_type == VDIR)
		return EISDIR;
	if (vp->v_type != VREG)
		return EINVAL;
	if (uio->uio_offset < 0)
		return EINVAL;

	if (uio->uio_resid == 0)
		return 0;

	if (ioflag & IO_APPEND)
		uio->uio_offset = np->rn_size;

	if (size_t(uio->uio_offset + uio->uio_resid) > (size_t)vp->v_size) {
		/* Expand the file size before writing to it */
		off_t end_pos = uio->uio_offset + uio->uio_resid;
		if (end_pos > (off_t)np->rn_bufsize) {
			// XXX: this could use a page level allocator
			size_t new_size = round_page(end_pos);
			void *new_buf = malloc(new_size);
			if (!new_buf)
				return EIO;
			if (np->rn_size != 0) {
				memcpy(new_buf, np->rn_buf, vp->v_size);
				free(np->rn_buf);
			}
			np->rn_buf = (char*)new_buf;
			np->rn_bufsize = new_size;
		}
		np->rn_size = end_pos;
		vp->v_size = end_pos;
	}
	return uiomove(np->rn_buf + uio->uio_offset, uio->uio_resid, uio);
}

static int
ramfs_rename(struct vnode *dvp1, struct vnode *vp1, char *name1,
	     struct vnode *dvp2, struct vnode *vp2, char *name2)
{
	struct ramfs_node *np, *old_np;
	int error;

	if (vp2) {
		/* Remove destination file, first */
		error = ramfs_remove_node((ramfs_node*)dvp2->v_data, (ramfs_node*)vp2->v_data);
		if (error)
			return error;
	}
	/* Same directory ? */
	if (dvp1 == dvp2) {
		/* Change the name of existing file */
		error = ramfs_rename_node((ramfs_node*)vp1->v_data, name2);
		if (error)
			return error;
	} else {
		/* Create new file or directory */
		old_np = (ramfs_node*)vp1->v_data;
		np = ramfs_add_node((ramfs_node*)dvp2->v_data, name2, VREG);
		if (np == NULL)
			return ENOMEM;

		if (vp1->v_type == VREG) {
			/* Copy file data */
			np->rn_buf = old_np->rn_buf;
			np->rn_size = old_np->rn_size;
			np->rn_bufsize = old_np->rn_bufsize;
			old_np->rn_buf = NULL;
		}
		/* Remove source file */
		ramfs_remove_node((ramfs_node*)dvp1->v_data, (ramfs_node*)vp1->v_data);
	}
	return 0;
}

/*
 * @vp: vnode of the directory.
 */
static int
ramfs_readdir(struct vnode *vp, struct file *fp, struct dirent *dir)
{
	struct ramfs_node *np, *dnp;
	int i;

	mutex_lock(&ramfs_lock);

	if (fp->f_offset == 0) {
		dir->d_type = DT_DIR;
		strlcpy((char *)&dir->d_name, ".", sizeof(dir->d_name));
	} else if (fp->f_offset == 1) {
		dir->d_type = DT_DIR;
		strlcpy((char *)&dir->d_name, "..", sizeof(dir->d_name));
	} else {
		dnp = (ramfs_node*)vp->v_data;
		np = dnp->rn_child;
		if (np == NULL) {
			mutex_unlock(&ramfs_lock);
			return ENOENT;
		}

		for (i = 0; i != (fp->f_offset - 2); i++) {
			np = np->rn_next;
			if (np == NULL) {
				mutex_unlock(&ramfs_lock);
				return ENOENT;
			}
		}
		if (np->rn_type == VDIR)
			dir->d_type = DT_DIR;
		else
			dir->d_type = DT_REG;
		strlcpy((char *)&dir->d_name, np->rn_name,
			sizeof(dir->d_name));
	}
	dir->d_fileno = fp->f_offset;
//	dir->d_namelen = strlen(dir->d_name);

	fp->f_offset++;

	mutex_unlock(&ramfs_lock);
	return 0;
}

extern "C"
int
ramfs_init(void)
{
	return 0;
}

static int
ramfs_getattr(struct vnode *vnode, struct vattr *attr)
{
	attr->va_nodeid = vnode->v_ino;
	attr->va_size = vnode->v_size;
	return 0;
}

#define ramfs_open	((vnop_open_t)vop_nullop)
#define ramfs_close	((vnop_close_t)vop_nullop)
#define ramfs_seek	((vnop_seek_t)vop_nullop)
#define ramfs_ioctl	((vnop_ioctl_t)vop_einval)
#define ramfs_fsync	((vnop_fsync_t)vop_nullop)
#define ramfs_setattr	((vnop_setattr_t)vop_eperm)
#define ramfs_inactive	((vnop_inactive_t)vop_nullop)
#define ramfs_link	((vnop_link_t)vop_eperm)

/*
 * vnode operations
 */
struct vnops ramfs_vnops = {
	ramfs_open,		/* open */
	ramfs_close,		/* close */
	ramfs_read,		/* read */
	ramfs_write,		/* write */
	ramfs_seek,		/* seek */
	ramfs_ioctl,		/* ioctl */
	ramfs_fsync,		/* fsync */
	ramfs_readdir,		/* readdir */
	ramfs_lookup,		/* lookup */
	ramfs_create,		/* create */
	ramfs_remove,		/* remove */
	ramfs_rename,		/* remame */
	ramfs_mkdir,		/* mkdir */
	ramfs_rmdir,		/* rmdir */
	ramfs_getattr,		/* getattr */
	ramfs_setattr,		/* setattr */
	ramfs_inactive,		/* inactive */
	ramfs_truncate,		/* truncate */
	ramfs_link,		/* link */
};

