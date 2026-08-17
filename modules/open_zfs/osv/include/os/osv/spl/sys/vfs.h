// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL vfs.h - standalone VFS types for OpenZFS.
 *
 * Provides vfs_t and related definitions without chaining through
 * the old compat vfs.h (which includes sys/vnode.h and triggers
 * the contrib vnode.h xoptattr conflict).
 */
#ifndef _SPL_OSV_VFS_H
#define	_SPL_OSV_VFS_H

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/vnode.h>

typedef	struct mount	vfs_t;

#define	vfs_flag	m_flags
#define	vfs_data	m_data
#define	vfs_fsid	m_fsid
#define	v_vfsp		v_mount

#define	VFS_RDONLY	MNT_RDONLY
#define	VFS_NOSETUID	MNT_NOSUID
#define	VFS_NOEXEC	MNT_NOEXEC

#define	VFS_HOLD(vfsp)	vfs_busy(vfsp)
#define	VFS_RELE(vfsp)	vfs_unbusy(vfsp)

#endif /* _SPL_OSV_VFS_H */
