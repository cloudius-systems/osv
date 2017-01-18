/*-
 * Copyright (c) 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)mount.h	8.21 (Berkeley) 5/20/95
 */

#ifndef _SYS_MOUNT_H_
#define _SYS_MOUNT_H_

#include <sys/cdefs.h>
#include <sys/statfs.h>
#include <osv/vnode.h>
#include <bsd/sys/sys/queue.h>

__BEGIN_DECLS

#ifdef _KERNEL

/*
 * Mount data
 */
struct mount {
	struct vfsops	*m_op;		/* pointer to vfs operation */
	int		m_flags;	/* mount flag */
	int		m_count;	/* reference count */
	char            m_path[PATH_MAX]; /* mounted path */
	char            m_special[PATH_MAX]; /* resource */
	struct device	*m_dev;		/* mounted device */
	struct dentry	*m_root;	/* root vnode */
	struct dentry	*m_covered;	/* vnode covered on parent fs */
	void		*m_data;	/* private data for fs */
	fsid_t 		m_fsid; 	/* id that uniquely identifies the fs */
};

#endif

/*
 * Mount flags.
 */
#define	MNT_RDONLY	0x00000001	/* read only filesystem */
#define	MNT_SYNCHRONOUS	0x00000002	/* file system written synchronously */
#define	MNT_NOEXEC	0x00000004	/* can't exec from filesystem */
#define	MNT_NOSUID	0x00000008	/* don't honor setuid bits on fs */
#define	MNT_NODEV	0x00000010	/* don't interpret special files */
#define	MNT_UNION	0x00000020	/* union with underlying filesystem */
#define	MNT_ASYNC	0x00000040	/* file system written asynchronously */

/*
 * Unmount flags.
 */
#define MNT_FORCE	0x00000001	/* forced unmount */

/*
 * exported mount flags.
 */
#define	MNT_EXRDONLY	0x00000080	/* exported read only */
#define	MNT_EXPORTED	0x00000100	/* file system is exported */
#define	MNT_DEFEXPORTED	0x00000200	/* exported to the world */
#define	MNT_EXPORTANON	0x00000400	/* use anon uid mapping for everyone */
#define	MNT_EXKERB	0x00000800	/* exported with Kerberos uid mapping */

/*
 * Flags set by internal operations.
 */
#define	MNT_LOCAL	0x00001000	/* filesystem is stored locally */
#define	MNT_QUOTA	0x00002000	/* quotas are enabled on filesystem */
#define	MNT_ROOTFS	0x00004000	/* identifies the root filesystem */

/*
 * Mask of flags that are visible to statfs()
 */
#define	MNT_VISFLAGMASK	0x0000ffff

#ifdef _KERNEL

/*
 * Filesystem type switch table.
 */
struct vfssw {
	const char      *vs_name;	/* name of file system */
	int		(*vs_init)(void); /* initialize routine */
	struct vfsops	*vs_op;		/* pointer to vfs operation */
};

/*
 * Operations supported on virtual file system.
 */
struct vfsops {
	int (*vfs_mount)	(struct mount *, const char *, int, const void *);
	int (*vfs_unmount)	(struct mount *, int flags);
	int (*vfs_sync)		(struct mount *);
	int (*vfs_vget)		(struct mount *, struct vnode *);
	int (*vfs_statfs)	(struct mount *, struct statfs *);
	struct vnops	*vfs_vnops;
};

typedef int (*vfsop_mount_t)(struct mount *, const char *, int, const void *);
typedef int (*vfsop_umount_t)(struct mount *, int flags);
typedef int (*vfsop_sync_t)(struct mount *);
typedef int (*vfsop_vget_t)(struct mount *, struct vnode *);
typedef int (*vfsop_statfs_t)(struct mount *, struct statfs *);

/*
 * VFS interface
 */
#define VFS_MOUNT(MP, DEV, FL, DAT) ((MP)->m_op->vfs_mount)(MP, DEV, FL, DAT)
#define VFS_UNMOUNT(MP, FL)         ((MP)->m_op->vfs_unmount)(MP, FL)
#define VFS_SYNC(MP)                ((MP)->m_op->vfs_sync)(MP)
#define VFS_VGET(MP, VP)            ((MP)->m_op->vfs_vget)(MP, VP)
#define VFS_STATFS(MP, SFP)         ((MP)->m_op->vfs_statfs)(MP, SFP)

#define VFS_NULL		    ((void *)vfs_null)

int	vfs_nullop(void);
int	vfs_einval(void);

void	 vfs_busy(struct mount *mp);
void	 vfs_unbusy(struct mount *mp);

void	 release_mp_dentries(struct mount *mp);

#endif

__END_DECLS

#ifdef __cplusplus

#include <vector>
#include <string>

namespace osv {

struct mount_desc {
    std::string special;
    std::string path;
    std::string type;
    std::string options;
};

std::vector<mount_desc> current_mounts();

}

#endif

#endif	/* !_SYS_MOUNT_H_ */
