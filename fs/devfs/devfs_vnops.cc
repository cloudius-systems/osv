/*
 * Copyright (c) 2005-2007, Kohsuke Ohtani
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
 * devfs - device file system.
 */

#include <sys/stat.h>

#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>

#include <osv/prex.h>
#include <osv/device.h>
#include <osv/vnode.h>
#include <osv/mount.h>
#include <osv/dentry.h>

#include "devfs.h"

#ifdef DEBUG_DEVFS
#define DPRINTF(a)	dprintf a
#else
#define DPRINTF(a)	do {} while (0)
#endif

#define ASSERT(e)	assert(e)

static uint64_t inode_count = 1; /* inode 0 is reserved to root */

static int
devfs_open(struct file *fp)
{
	struct vnode *vp = fp->f_dentry->d_vnode;
	char *path = fp->f_dentry->d_path;
	struct device *dev;
	int error;

	DPRINTF(("devfs_open: path=%s\n", path));

	if (!strcmp(path, "/"))	/* root ? */
		return 0;

	if (vp->v_flags & VPROTDEV) {
		DPRINTF(("devfs_open: failed to open protected device.\n"));
		return EPERM;
	}
	if (*path == '/')
		path++;
	error = device_open(path, fp->f_flags & DO_RWMASK, &dev);
	if (error) {
		DPRINTF(("devfs_open: can not open device = %s error=%d\n",
			 path, error));
		return error;
	}
	vp->v_data = (void *)dev;	/* Store private data */
	return 0;
}

static int
devfs_close(struct vnode *vp, struct file *fp)
{

	DPRINTF(("devfs_close: fp=%x\n", fp));

	if (!strcmp(fp->f_dentry->d_path, "/"))	/* root ? */
		return 0;

	return device_close((device*)vp->v_data);
}

static int
devfs_read(struct vnode *vp, struct file *fp, struct uio *uio, int ioflags)
{
	return device_read((device*)vp->v_data, uio, ioflags);
}

static int
devfs_write(struct vnode *vp, struct uio *uio, int ioflags)
{
	return device_write((device*)vp->v_data, uio, ioflags);
}

static int
devfs_ioctl(struct vnode *vp, struct file *fp, u_long cmd, void *arg)
{
	int error;

	error = device_ioctl((device*)vp->v_data, cmd, arg);
	DPRINTF(("devfs_ioctl: cmd=%x\n", cmd));
	return error;
}

static int
devfs_lookup(struct vnode *dvp, char *name, struct vnode **vpp)
{
	struct devinfo info;
	struct vnode *vp;
	int error, i;

	DPRINTF(("devfs_lookup:%s\n", name));

	*vpp = NULL;

	if (*name == '\0')
		return ENOENT;

	i = 0;
	error = 0;
	info.cookie = 0;
	for (;;) {
		error = device_info(&info);
		if (error) {
			return ENOENT;
		}
		if (!strncmp(info.name, name, MAXDEVNAME))
			break;
		i++;
	}
	if (vget(dvp->v_mount, inode_count++, &vp)) {
		/* found in cache */
		*vpp = vp;
		return 0;
	}
	if (!vp)
		return ENOMEM;
	vp->v_type = (info.flags & D_CHR) ? VCHR : VBLK;
	if (info.flags & D_TTY)
		vp->v_flags |= VISTTY;

	vp->v_mode = (mode_t)(S_IRUSR | S_IWUSR);

	*vpp = vp;

	return 0;
}

/*
 * @vp: vnode of the directory.
 */
static int
devfs_readdir(struct vnode *vp, struct file *fp, struct dirent *dir)
{
	struct devinfo info;
	int error, i;

	DPRINTF(("devfs_readdir offset=%d\n", fp->f_offset));

	i = 0;
	error = 0;
	info.cookie = 0;
	do {
		error = device_info(&info);
		if (error)
			return ENOENT;
	} while (i++ != fp->f_offset);

	dir->d_type = 0;
	if (info.flags & D_CHR)
		dir->d_type = DT_CHR;
	else if (info.flags & D_BLK)
		dir->d_type = DT_BLK;
	strlcpy((char *)&dir->d_name, info.name, sizeof(dir->d_name));
	dir->d_fileno = fp->f_offset;
//	dir->d_namlen = strlen(dir->d_name);

	DPRINTF(("devfs_readdir: %s\n", dir->d_name));
	fp->f_offset++;
	return 0;
}

static int
devfs_unmount(struct mount *mp, int flags)
{
	release_mp_dentries(mp);
	return 0;
}

extern "C"
int
devfs_init(void)
{
	return 0;
}

static int
devfs_getattr(struct vnode *vnode, struct vattr *attr)
{
	attr->va_nodeid = vnode->v_ino;
	attr->va_size = vnode->v_size;
	return 0;
}

#define devfs_mount	((vfsop_mount_t)vfs_nullop)
#define devfs_sync	((vfsop_sync_t)vfs_nullop)
#define devfs_vget	((vfsop_vget_t)vfs_nullop)
#define devfs_statfs	((vfsop_statfs_t)vfs_nullop)

#define devfs_seek	((vnop_seek_t)vop_nullop)
#define devfs_fsync	((vnop_fsync_t)vop_nullop)
#define devfs_create	((vnop_create_t)vop_einval)
#define devfs_remove	((vnop_remove_t)vop_einval)
#define devfs_rename	((vnop_rename_t)vop_einval)
#define devfs_mkdir	((vnop_mkdir_t)vop_einval)
#define devfs_rmdir	((vnop_rmdir_t)vop_einval)
#define devfs_setattr	((vnop_setattr_t)vop_eperm)
#define devfs_inactive	((vnop_inactive_t)vop_nullop)
#define devfs_truncate	((vnop_truncate_t)vop_nullop)
#define devfs_link	((vnop_link_t)vop_eperm)

/*
 * vnode operations
 */
struct vnops devfs_vnops = {
	devfs_open,		/* open */
	devfs_close,		/* close */
	devfs_read,		/* read */
	devfs_write,		/* write */
	devfs_seek,		/* seek */
	devfs_ioctl,		/* ioctl */
	devfs_fsync,		/* fsync */
	devfs_readdir,		/* readdir */
	devfs_lookup,		/* lookup */
	devfs_create,		/* create */
	devfs_remove,		/* remove */
	devfs_rename,		/* remame */
	devfs_mkdir,		/* mkdir */
	devfs_rmdir,		/* rmdir */
	devfs_getattr,		/* getattr */
	devfs_setattr,		/* setattr */
	devfs_inactive,		/* inactive */
	devfs_truncate,		/* truncate */
	devfs_link,		/* link */
};

/*
 * File system operations
 */
struct vfsops devfs_vfsops = {
	devfs_mount,		/* mount */
	devfs_unmount,		/* unmount */
	devfs_sync,		/* sync */
	devfs_vget,		/* vget */
	devfs_statfs,		/* statfs */
	&devfs_vnops,		/* vnops */
};
