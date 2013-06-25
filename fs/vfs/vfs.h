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

#ifndef _VFS_H
#define _VFS_H

#include <sys/cdefs.h>
#include <assert.h>
#include <dirent.h>
#include <limits.h>

#include <osv/prex.h>
#include <osv/file.h>
#include <osv/mount.h>
#include <osv/vnode.h>

/* #define DEBUG_VFS 1 */

/*
 * Tunable parameters
 */
#define FSMAXNAMES	16		/* max length of 'file system' name */

#ifdef DEBUG_VFS
extern int vfs_debug;

#define	VFSDB_CORE	0x00000001
#define	VFSDB_SYSCALL	0x00000002
#define	VFSDB_VNODE	0x00000004
#define	VFSDB_BIO	0x00000008
#define	VFSDB_CAP	0x00000010

#define VFSDB_FLAGS	0x00000013

#define	DPRINTF(_m,X)	if (vfs_debug & (_m)) kprintf X
#else
#define	DPRINTF(_m, X)
#endif

#define ASSERT(e)	assert(e)

#define OPEN_MAX	256

/*
 * per task data
 */
struct task {
	char 	    t_cwd[PATH_MAX];	/* current working directory */
	struct file *t_cwdfp;		/* directory for cwd */
};

extern const struct vfssw vfssw[];

__BEGIN_DECLS
int	 sys_open(char *path, int flags, mode_t mode, struct file *fp);
int	 sys_read(struct file *fp, struct iovec *iov, size_t niov,
		off_t offset, size_t *count);
int	 sys_write(struct file *fp, struct iovec *iov, size_t niov,
		off_t offset, size_t *count);
int	 sys_lseek(struct file *fp, off_t off, int type, off_t * cur_off);
int	 sys_ioctl(struct file *fp, u_long request, void *buf);
int	 sys_fstat(struct file *fp, struct stat *st);
int	 sys_fstatfs(struct file *fp, struct statfs *buf);
int	 sys_fsync(struct file *fp);
int	 sys_ftruncate(struct file *fp, off_t length);

int	 sys_readdir(struct file *fp, struct dirent *dirent);
int	 sys_rewinddir(struct file *fp);
int	 sys_seekdir(struct file *fp, long loc);
int	 sys_telldir(struct file *fp, long *loc);
int	 sys_fchdir(struct file *fp, char *path);

int	 sys_mkdir(char *path, mode_t mode);
int	 sys_rmdir(char *path);
int	 sys_mknod(char *path, mode_t mode);
int	 sys_rename(char *src, char *dest);
int	 sys_unlink(char *path);
int	 sys_access(char *path, int mode);
int	 sys_stat(char *path, struct stat *st);
int	 sys_statfs(char *path, struct statfs *buf);
int	 sys_truncate(char *path, off_t length);
ssize_t	 sys_readlink(char *path, char *buf, size_t bufsize);

int	 sys_mount(char *dev, char *dir, char *fsname, int flags, void *data);
int	 sys_umount(char *path);
int	 sys_sync(void);


int	 task_alloc(struct task **pt);
int	 task_conv(struct task *t, const char *path, int mode, char *full);

//int	 sec_file_permission(task_t task, char *path, int mode);
int	 sec_vnode_permission(char *path);

int	 namei(char *path, struct vnode **vpp);
int	 lookup(char *path, struct vnode **vpp, char **name);
void	 vnode_init(void);

int	 vfs_findroot(char *path, struct mount **mp, char **root);

int	 fs_noop(void);

#ifdef DEBUG_VFS
void	 vnode_dump(void);
void	 mount_dump(void);
#endif
__END_DECLS

#endif /* !_VFS_H */
