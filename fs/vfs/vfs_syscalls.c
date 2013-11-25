/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

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
 * vfs_syscalls.c - everything in this file is a routine implementing
 *                  a VFS system call.
 */

#include <sys/stat.h>
#include <dirent.h>

#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#include <osv/prex.h>
#include <osv/vnode.h>
#include "vfs.h"

int
sys_open(char *path, int flags, mode_t mode, struct file *fp)
{
	struct dentry *dp, *ddp;
	struct vnode *vp;
	char *filename;
	int error;

	DPRINTF(VFSDB_SYSCALL, ("sys_open: path=%s flags=%x mode=%x\n",
				path, flags, mode));

	flags = fflags(flags);
	if (flags & O_CREAT) {
		error = namei(path, &dp);
		if (error == ENOENT) {
			/* Create new file. */
			if ((error = lookup(path, &ddp, &filename)) != 0)
				return error;

			vn_lock(ddp->d_vnode);
			if ((error = vn_access(ddp->d_vnode, VWRITE)) != 0) {
				vn_unlock(ddp->d_vnode);
				drele(ddp);
				return error;
			}
			mode &= ~S_IFMT;
			mode |= S_IFREG;
			error = VOP_CREATE(ddp->d_vnode, filename, mode);
			vn_unlock(ddp->d_vnode);
			drele(ddp);

			if (error)
				return error;
			if ((error = namei(path, &dp)) != 0)
				return error;

			vp = dp->d_vnode;
			flags &= ~O_TRUNC;
		} else if (error) {
			return error;
		} else {
			/* File already exits */
			if (flags & O_EXCL) {
				error = EEXIST;
				goto out_drele;
			}
		}

		vp = dp->d_vnode;
		flags &= ~O_CREAT;
	} else {
		/* Open */
		error = namei(path, &dp);
		if (error)
			return error;

		vp = dp->d_vnode;

		if (flags & FWRITE || flags & O_TRUNC) {
			error = vn_access(vp, VWRITE);
			if (error)
				goto out_drele;

			error = EISDIR;
			if (vp->v_type == VDIR)
				goto out_drele;
		}
	}

	vn_lock(vp);
	/* Process truncate request */
	if (flags & O_TRUNC) {
		error = EINVAL;
		if (!(flags & FWRITE) || vp->v_type == VDIR)
			goto out_vn_unlock;

		error = VOP_TRUNCATE(vp, 0);
		if (error)
			goto out_vn_unlock;
	}

	finit(fp, flags, DTYPE_VNODE, NULL, &vfs_ops);
	fp->f_dentry = dp;

	error = VOP_OPEN(vp, fp);
	if (error)
		goto out_vn_unlock;
	vn_unlock(vp);

	return 0;

out_vn_unlock:
	vn_unlock(vp);
out_drele:
	drele(dp);
	return error;
}

int
sys_close(struct file *fp)
{

	return 0;
}

int
sys_read(struct file *fp, struct iovec *iov, size_t niov,
		off_t offset, size_t *count)
{
	struct uio *uio = NULL;
	ssize_t bytes;
	int error;

	DPRINTF(VFSDB_SYSCALL, ("sys_write: fp=%x buf=%x size=%d\n",
				(u_int)fp, (u_int)buf, size));

	if ((fp->f_flags & FREAD) == 0)
		return EBADF;

	error = copyinuio(iov, niov, &uio);
	if (error)
		return error;

	bytes = uio->uio_resid;

	if (uio->uio_resid == 0) {
		*count = 0;
		free(uio);
		return 0;
	}

	uio->uio_rw = UIO_READ;
	uio->uio_offset = offset;
	error = fo_read(fp, uio, (offset == -1) ? 0 : FOF_OFFSET);
	*count = bytes - uio->uio_resid;
	free(uio);

	return error;
}

int
sys_write(struct file *fp, struct iovec *iov, size_t niov,
		off_t offset, size_t *count)
{
	struct uio *uio = NULL;
	ssize_t bytes;
	int error;

	DPRINTF(VFSDB_SYSCALL, ("sys_write: fp=%x uio=%x niv=%zu\n",
				(u_long)fp, (u_long)uio, niv));

	if ((fp->f_flags & FWRITE) == 0)
		return EBADF;

	error = copyinuio(iov, niov, &uio);
	if (error)
		return error;

	if (uio->uio_resid == 0) {
		*count = 0;
		free(uio);
		return 0;
	}

	bytes = uio->uio_resid;

	uio->uio_rw = UIO_WRITE;
	uio->uio_offset = offset;
	error = fo_write(fp, uio, (offset == -1) ? 0 : FOF_OFFSET);
	*count = bytes - uio->uio_resid;
	free(uio);

	return error;
}

int
sys_lseek(struct file *fp, off_t off, int type, off_t *origin)
{
	struct vnode *vp;

	DPRINTF(VFSDB_SYSCALL, ("sys_seek: fp=%x off=%d type=%d\n",
				(u_int)fp, (u_int)off, type));

	if (!fp->f_dentry) {
	    // Linux doesn't implement lseek() on pipes, sockets, or ttys.
	    // In OSV, we only implement lseek() on regular files, backed by vnode
	    return ESPIPE;
	}

	vp = fp->f_dentry->d_vnode;
	vn_lock(vp);
	switch (type) {
	case SEEK_SET:
		if (off < 0)
			off = 0;
		break;
	case SEEK_CUR:
		if (fp->f_offset + off > vp->v_size)
			off = vp->v_size;
		else if (fp->f_offset + off < 0)
			off = 0;
		else
			off = fp->f_offset + off;
		break;
	case SEEK_END:
		if (off > 0)
			off = vp->v_size;
		else if (vp->v_size + off < 0)
			off = 0;
		else
			off = vp->v_size + off;
		break;
	default:
		vn_unlock(vp);
		return EINVAL;
	}
	/* Request to check the file offset */
	if (VOP_SEEK(vp, fp, fp->f_offset, off) != 0) {
		vn_unlock(vp);
		return EINVAL;
	}
	*origin = off;
	fp->f_offset = off;
	vn_unlock(vp);
	return 0;
}

int
sys_ioctl(struct file *fp, u_long request, void *buf)
{
	int error;

	DPRINTF(VFSDB_SYSCALL, ("sys_ioctl: fp=%x request=%x\n", fp, request));

	if ((fp->f_flags & (FREAD | FWRITE)) == 0)
		return EBADF;

	error = fo_ioctl(fp, request, buf);

	DPRINTF(VFSDB_SYSCALL, ("sys_ioctl: comp error=%d\n", error));
	return error;
}

int
sys_fsync(struct file *fp)
{
	struct vnode *vp;
	int error;

	DPRINTF(VFSDB_SYSCALL, ("sys_fsync: fp=%x\n", fp));

	if (!fp->f_dentry)
		return EINVAL;

	vp = fp->f_dentry->d_vnode;
	vn_lock(vp);
	error = VOP_FSYNC(vp, fp);
	vn_unlock(vp);
	return error;
}

int
sys_fstat(struct file *fp, struct stat *st)
{
	int error = 0;

	DPRINTF(VFSDB_SYSCALL, ("sys_fstat: fp=%x\n", fp));

	error = fo_stat(fp, st);

	return error;
}

/*
 * Return 0 if directory is empty
 */
static int
check_dir_empty(char *path)
{
	int error;
	struct file *fp;
	struct dirent dir;

	DPRINTF(VFSDB_SYSCALL, ("check_dir_empty\n"));

	error = falloc_noinstall(&fp);
	if (error)
		return error;

	error = sys_open(path, O_RDONLY, 0, fp);
	if (error)
		goto out_fdrop;

	do {
		error = sys_readdir(fp, &dir);
		if (error != 0 && error != EACCES)
			break;
	} while (!strcmp(dir.d_name, ".") || !strcmp(dir.d_name, ".."));

	if (error == ENOENT)
		error = 0;
	else if (error == 0)
		error = EEXIST;
out_fdrop:
	fdrop(fp);
	return error;
}

int
sys_readdir(struct file *fp, struct dirent *dir)
{
	struct vnode *dvp;
	int error;

	DPRINTF(VFSDB_SYSCALL, ("sys_readdir: fp=%x\n", fp));

	if (!fp->f_dentry)
		return ENOTDIR;

	dvp = fp->f_dentry->d_vnode;
	vn_lock(dvp);
	if (dvp->v_type != VDIR) {
		vn_unlock(dvp);
		return EBADF;
	}
	error = VOP_READDIR(dvp, fp, dir);
	DPRINTF(VFSDB_SYSCALL, ("sys_readdir: error=%d path=%s\n",
				error, dir->d_name));
	vn_unlock(dvp);
	return error;
}

int
sys_rewinddir(struct file *fp)
{
	struct vnode *dvp;

	if (!fp->f_dentry)
		return ENOTDIR;

	dvp = fp->f_dentry->d_vnode;
	vn_lock(dvp);
	if (dvp->v_type != VDIR) {
		vn_unlock(dvp);
		return EBADF;
	}
	fp->f_offset = 0;
	vn_unlock(dvp);
	return 0;
}

int
sys_seekdir(struct file *fp, long loc)
{
	struct vnode *dvp;

	if (!fp->f_dentry)
		return ENOTDIR;

	dvp = fp->f_dentry->d_vnode;
	vn_lock(dvp);
	if (dvp->v_type != VDIR) {
		vn_unlock(dvp);
		return EBADF;
	}
	fp->f_offset = (off_t)loc;
	vn_unlock(dvp);
	return 0;
}

int
sys_telldir(struct file *fp, long *loc)
{
	struct vnode *dvp;

	if (!fp->f_dentry)
		return ENOTDIR;

	dvp = fp->f_dentry->d_vnode;
	vn_lock(dvp);
	if (dvp->v_type != VDIR) {
		vn_unlock(dvp);
		return EBADF;
	}
	*loc = (long)fp->f_offset;
	vn_unlock(dvp);
	return 0;
}

int
sys_mkdir(char *path, mode_t mode)
{
	char *name;
	struct dentry *dp, *ddp;
	int error;

	DPRINTF(VFSDB_SYSCALL, ("sys_mkdir: path=%s mode=%d\n",	path, mode));

	error = namei(path, &dp);
	if (!error) {
		/* File already exists */
		drele(dp);
		return EEXIST;
	}

	if ((error = lookup(path, &ddp, &name)) != 0) {
		/* Directory already exists */
		return error;
	}

	vn_lock(ddp->d_vnode);
	if ((error = vn_access(ddp->d_vnode, VWRITE)) != 0)
		goto out;
	mode &= ~S_IFMT;
	mode |= S_IFDIR;

	error = VOP_MKDIR(ddp->d_vnode, name, mode);
 out:
	vn_unlock(ddp->d_vnode);
	drele(ddp);
	return error;
}

int
sys_rmdir(char *path)
{
	struct dentry *dp, *ddp;
	struct vnode *vp;
	int error;
	char *name;

	DPRINTF(VFSDB_SYSCALL, ("sys_rmdir: path=%s\n", path));

	if ((error = check_dir_empty(path)) != 0)
		return error;
	error = namei(path, &dp);
	if (error)
		return error;

	vp = dp->d_vnode;
	vn_lock(vp);
	if ((error = vn_access(vp, VWRITE)) != 0)
		goto out;
	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}
	if (vp->v_flags & VROOT || vcount(vp) >= 2) {
		error = EBUSY;
		goto out;
	}
	if ((error = lookup(path, &ddp, &name)) != 0)
		goto out;

	vn_lock(ddp->d_vnode);
	error = VOP_RMDIR(ddp->d_vnode, vp, name);
	vn_unlock(ddp->d_vnode);

	vn_unlock(vp);
	drele(ddp);
	drele(dp);
	return error;

 out:
	vn_unlock(vp);
	drele(dp);
	return error;
}

int
sys_mknod(char *path, mode_t mode)
{
	char *name;
	struct dentry *dp, *ddp;
	int error;

	DPRINTF(VFSDB_SYSCALL, ("sys_mknod: path=%s mode=%d\n",	path, mode));

	switch (mode & S_IFMT) {
	case S_IFREG:
	case S_IFDIR:
	case S_IFIFO:
	case S_IFSOCK:
		/* OK */
		break;
	default:
		return EINVAL;
	}

	error = namei(path, &dp);
	if (!error) {
		drele(dp);
		return EEXIST;
	}

	if ((error = lookup(path, &ddp, &name)) != 0)
		return error;

	vn_lock(ddp->d_vnode);
	if ((error = vn_access(ddp->d_vnode, VWRITE)) != 0)
		goto out;
	if (S_ISDIR(mode))
		error = VOP_MKDIR(ddp->d_vnode, name, mode);
	else
		error = VOP_CREATE(ddp->d_vnode, name, mode);
 out:
	vn_unlock(ddp->d_vnode);
	drele(ddp);
	return error;
}

/*
 * Returns true when @parent path could represent parent directory
 * of a file or directory represented by @child path.
 *
 * Assumes both paths do not have trailing slashes.
 */
static bool
is_parent(const char *parent, const char *child)
{
	size_t p_len = strlen(parent);
	return !strncmp(parent, child, p_len) && (parent[p_len-1] == '/' || child[p_len] == '/');
}

static bool
has_trailing(const char *path, char ch)
{
	size_t len = strlen(path);
	return len && path[len - 1] == ch;
}

static void
strip_trailing(char *path, char ch)
{
	size_t len = strlen(path);

	while (len && path[len - 1] == ch)
		len--;

	path[len] = '\0';
}

int
sys_rename(char *src, char *dest)
{
	struct dentry *dp1, *dp2 = 0, *ddp1, *ddp2;
	struct vnode *vp1, *vp2 = 0, *dvp1, *dvp2;
	char *sname, *dname;
	int error;
	char root[] = "/";

	DPRINTF(VFSDB_SYSCALL, ("sys_rename: src=%s dest=%s\n", src, dest));

	error = namei(src, &dp1);
	if (error)
		return error;

	vp1 = dp1->d_vnode;
	vn_lock(vp1);

	if ((error = vn_access(vp1, VWRITE)) != 0)
		goto err1;

	/* Is the source busy ? */
	if (vcount(vp1) >= 2) {
		error = EBUSY;
		goto err1;
	}

	/* Check type of source & target */
	error = namei(dest, &dp2);
	if (error == 0) {
		/* target exists */

		vp2 = dp2->d_vnode;
		vn_lock(vp2);

		if (vp1->v_type == VDIR && vp2->v_type != VDIR) {
			error = ENOTDIR;
			goto err2;
		} else if (vp1->v_type != VDIR && vp2->v_type == VDIR) {
			error = EISDIR;
			goto err2;
		}
		if (vp2->v_type == VDIR && check_dir_empty(dest)) {
			error = EEXIST;
			goto err2;
		}

		if (vcount(vp2) >= 2) {
			error = EBUSY;
			goto err2;
		}
	} else if (error == ENOENT) {
		if (vp1->v_type != VDIR && has_trailing(dest, '/')) {
			error = ENOTDIR;
			goto err2;
		}
	} else {
		goto err2;
	}

	if (strcmp(dest, "/"))
		strip_trailing(dest, '/');

	if (strcmp(src, "/"))
		strip_trailing(src, '/');

	/* If source and dest are the same, do nothing */
	if (!strncmp(src, dest, PATH_MAX))
		goto err2;

	/* Check if target is directory of source */
	if (is_parent(src, dest)) {
		error = EINVAL;
		goto err2;
	}

	dname = strrchr(dest, '/');
	if (dname == NULL) {
		error = ENOTDIR;
		goto err2;
	}
	if (dname == dest)
		dest = root;

	*dname = 0;
	dname++;

	if ((error = lookup(src, &ddp1, &sname)) != 0)
		goto err2;

	dvp1 = ddp1->d_vnode;
	vn_lock(dvp1);

	if ((error = namei(dest, &ddp2)) != 0)
		goto err3;

	dvp2 = ddp2->d_vnode;
	vn_lock(dvp2);

	/* The source and dest must be same file system */
	if (dvp1->v_mount != dvp2->v_mount) {
		error = EXDEV;
		goto err4;
	}

	error = VOP_RENAME(dvp1, vp1, sname, dvp2, vp2, dname);
 err4:
	vn_unlock(dvp2);
	drele(ddp2);
 err3:
	vn_unlock(dvp1);
	drele(ddp1);
 err2:
	if (vp2) {
		vn_unlock(vp2);
		drele(dp2);
	}
 err1:
	vn_unlock(vp1);
	drele(dp1);
	return error;
}

int
sys_link(char *oldpath, char *newpath)
{
	struct dentry *olddp, *newdp, *newdirdp;
	struct vnode *vp;
	char *name;
	int error;

	DPRINTF(VFSDB_SYSCALL, ("sys_link: oldpath=%s newpath=%s\n",
				oldpath, newpath));

	/* File from oldpath must exist */
	if ((error = namei(oldpath, &olddp)) != 0)
		return error;

	vp = olddp->d_vnode;
	vn_lock(vp);

	if (vp->v_type == VDIR) {
		error = EPERM;
		goto out;
	}

	/* If newpath exists, it shouldn't be overwritten */
	if (!namei(newpath, &newdp)) {
		drele(newdp);
		error = EEXIST;
		goto out;
	}

	/* Get pointer to the parent dentry of newpath */
	if ((error = lookup(newpath, &newdirdp, &name)) != 0)
		goto out;

	vn_lock(newdirdp->d_vnode);

	/* Both files must reside on the same mounted file system */
	if (olddp->d_mount != newdirdp->d_mount) {
		error = EXDEV;
		goto out1;
	}

	/* Write access to the dir containing newpath is required */
	if ((error = vn_access(newdirdp->d_vnode, VWRITE)) != 0)
		goto out1;

	/* Map newpath into dentry hash with the same vnode as oldpath */
	if (!(newdp = dentry_alloc(vp, newpath))) {
		error = ENOMEM;
		goto out1;
	}

	if ((error = VOP_LINK(newdirdp->d_vnode, vp, name)) != 0) {
		drele(newdp);
		goto out1;
	}
 out1:
	vn_unlock(newdirdp->d_vnode);
	drele(newdirdp);
 out:
	vn_unlock(vp);
	drele(olddp);
	return error;
}

int
sys_unlink(char *path)
{
	char *name;
	struct dentry *dp, *ddp;
	struct vnode *vp;
	int error;

	DPRINTF(VFSDB_SYSCALL, ("sys_unlink: path=%s\n", path));

	if ((error = namei(path, &dp)) != 0)
		return error;

	vp = dp->d_vnode;
	vn_lock(vp);
	if ((error = vn_access(vp, VWRITE)) != 0)
		goto out;
	if (vp->v_type == VDIR) {
		error = EPERM;
		goto out;
	}
	/* XXX: Need to allow unlink for opened file. */
	if (vp->v_flags & VROOT || vcount(vp) >= 2) {
		error = EBUSY;
		goto out;
	}
	if ((error = lookup(path, &ddp, &name)) != 0)
		goto out;

	vn_lock(ddp->d_vnode);
	error = VOP_REMOVE(ddp->d_vnode, vp, name);
	vn_unlock(ddp->d_vnode);

	vn_unlock(vp);
	drele(ddp);
	drele(dp);
	return 0;
 out:
	vn_unlock(vp);
	drele(dp);
	return error;
}

int
sys_access(char *path, int mode)
{
	struct dentry *dp;
	int error, flags;

	DPRINTF(VFSDB_SYSCALL, ("sys_access: path=%s mode=%x\n", path, mode));

	/* If F_OK is set, we return here if file is not found. */
	error = namei(path, &dp);
	if (error)
		return error;

	flags = 0;
	if (mode & R_OK)
		flags |= VREAD;
	if (mode & W_OK)
		flags |= VWRITE;
	if (mode & X_OK)
		flags |= VEXEC;

	error = vn_access(dp->d_vnode, flags);

	drele(dp);
	return error;
}

int
sys_stat(char *path, struct stat *st)
{
	struct dentry *dp;
	int error;

	DPRINTF(VFSDB_SYSCALL, ("sys_stat: path=%s\n", path));

	error = namei(path, &dp);
	if (error)
		return error;
	error = vn_stat(dp->d_vnode, st);
	drele(dp);
	return error;
}

int
sys_statfs(char *path, struct statfs *buf)
{
	struct dentry *dp;
	int error;

	memset(buf, 0, sizeof(*buf));

	error = namei(path, &dp);
	if (error)
		return error;

	error = VFS_STATFS(dp->d_mount, buf);
	drele(dp);

	return error;
}

int
sys_fstatfs(struct file *fp, struct statfs *buf)
{
	struct vnode *vp;
	int error = 0;

	if (!fp->f_dentry)
		return EBADF;

	vp = fp->f_dentry->d_vnode;
	memset(buf, 0, sizeof(*buf));

	vn_lock(vp);
	error = VFS_STATFS(vp->v_mount, buf);
	vn_unlock(vp);

	return error;
}

int
sys_truncate(char *path, off_t length)
{
	struct dentry *dp;
	int error;

	error = namei(path, &dp);
	if (error)
		return error;

	vn_lock(dp->d_vnode);
	error = VOP_TRUNCATE(dp->d_vnode, length);
	vn_unlock(dp->d_vnode);

	drele(dp);
	return error;
}

int
sys_ftruncate(struct file *fp, off_t length)
{
	struct vnode *vp;
	int error;

	if (!fp->f_dentry)
		return EBADF;

	vp = fp->f_dentry->d_vnode;
	vn_lock(vp);
	error = VOP_TRUNCATE(vp, length);
	vn_unlock(vp);

	return error;
}

int
sys_fchdir(struct file *fp, char *cwd)
{
	struct vnode *dvp;

	if (!fp->f_dentry)
		return EBADF;

	dvp = fp->f_dentry->d_vnode;
	vn_lock(dvp);
	if (dvp->v_type != VDIR) {
		vn_unlock(dvp);
		return EBADF;
	}
	strlcpy(cwd, fp->f_dentry->d_path, PATH_MAX);
	vn_unlock(dvp);
	return 0;
}

ssize_t
sys_readlink(char *path, char *buf, size_t bufsize)
{
	int error;
	struct dentry *dp;

	error = namei(path, &dp);
	if (error)
		return error;

	/* no symlink support (yet) in OSv */
	drele(dp);
	return EINVAL;
}

/*
 * Check the validity of the members of a struct timeval.
 */
static bool is_timeval_valid(const struct timeval *time)
{
    return (time->tv_sec >= 0) &&
           (time->tv_usec >= 0 && time->tv_usec < 1000000);
}

/*
 * Convert a timeval struct to a timespec one.
 */
static void convert_timeval(struct timespec *to, const struct timeval *from)
{
    to->tv_sec = from->tv_sec;
    to->tv_nsec = from->tv_usec * 1000; // Convert microseconds to nanoseconds
}

int
sys_utimes(char *path, const struct timeval times[2])
{
    int error;
    struct dentry *dp;
    struct timespec timespec_times[2];

    DPRINTF(VFSDB_SYSCALL, ("sys_utimes: path=%s\n", path));

    if (!is_timeval_valid(&times[0]) || !is_timeval_valid(&times[1]))
        return EINVAL;

    // Convert each element of timeval array to the timespec type
    convert_timeval(&timespec_times[0], &times[0]);
    convert_timeval(&timespec_times[1], &times[1]);

    error = namei(path, &dp);
    if (error)
        return error;

    if (dp->d_mount->m_flags & MNT_RDONLY) {
        error = EROFS;
    } else {
        error = vn_settimes(dp->d_vnode, timespec_times);
    }

    drele(dp);
    return error;
}
