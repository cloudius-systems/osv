// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL vnode.h - standalone vnode types for OpenZFS.
 *
 * This header provides vnode_t, vattr_t, AT_* constants, and related
 * definitions that OpenZFS needs, WITHOUT the xoptattr/xvattr/vsecattr
 * types which are provided by OpenZFS's own xvattr.h.
 *
 * We include osv/vnode.h directly for the core struct vnode definition,
 * then add the Solaris compatibility layer on top.
 */
#ifndef _SPL_OSV_VNODE_H
#define	_SPL_OSV_VNODE_H

#include <osv/vnode.h>

struct vnode;
struct vattr;

typedef	struct vnode	vnode_t;
typedef	struct vattr	vattr_t;
typedef	enum vtype	vtype_t;

#define	IS_DEVVP(vp)	\
	((vp)->v_type == VCHR || (vp)->v_type == VBLK || (vp)->v_type == VFIFO)

#define	V_XATTRDIR	0x0000	/* attribute unnamed directory */

/*
 * Import vnode attributes flags (AT_TYPE, AT_MODE, AT_UID, etc.)
 */
#include <osv/vnode_attr.h>

/*
 * AT_XVATTR - if set in va_mask, the structure is an xvattr.
 */
#ifndef AT_XVATTR
#define	AT_XVATTR	0x10000
#endif

/*
 * ATTR_* - Linux-style attribute names used by some ZFS code.
 * Map them to AT_* constants.
 */
#ifndef ATTR_UID
#define	ATTR_UID	AT_UID
#define	ATTR_GID	AT_GID
#define	ATTR_MODE	AT_MODE
#define	ATTR_XVATTR	AT_XVATTR
#define	ATTR_CTIME	AT_CTIME
#define	ATTR_MTIME	AT_MTIME
#define	ATTR_ATIME	AT_ATIME
#endif

#define	AT_ALL		(AT_TYPE|AT_MODE|AT_UID|AT_GID|AT_FSID|AT_NODEID|\
			AT_NLINK|AT_SIZE|AT_ATIME|AT_MTIME|AT_CTIME|\
			AT_RDEV|AT_BLKSIZE|AT_NBLOCKS|AT_SEQ)

#define	AT_STAT		(AT_MODE|AT_UID|AT_GID|AT_FSID|AT_NODEID|AT_NLINK|\
			AT_SIZE|AT_ATIME|AT_MTIME|AT_CTIME|AT_RDEV|AT_TYPE)

#define	AT_TIMES	(AT_ATIME|AT_MTIME|AT_CTIME)

#define	AT_NOSET	(AT_NLINK|AT_RDEV|AT_FSID|AT_NODEID|AT_TYPE|\
			AT_BLKSIZE|AT_NBLOCKS|AT_SEQ)

/*
 * Flags for VOP_ACCESS
 */
#define	V_ACE_MASK	0x1	/* mask represents NFSv4 ACE permissions */
#ifndef V_APPEND
#define	V_APPEND	0x2	/* want to do append only check */
#endif

/*
 * Flags for vnode operations - case-insensitive lookup
 */
#ifndef FIGNORECASE
#define	FIGNORECASE	0x00	/* case-insensitive lookup (unused on OSv) */
#endif

/*
 * Flags for vnode operations.
 */
enum rm		{ RMFILE, RMDIRECTORY };	/* rm or rmdir (remove) */
enum create	{ CRCREAT, CRMKNOD, CRMKDIR };	/* reason for create */

/*
 * Caller context (simplified for OSv).
 */
typedef struct caller_context {
	pid_t		cc_pid;
	int		cc_sysid;
	u_longlong_t	cc_caller_id;
	ulong_t		cc_flags;
} caller_context_t;

/*
 * Vnode reference management.
 */
#define	VN_RELE(v)	vrele(v)
#define	VN_RELE_ASYNC(vp, taskq)	vrele(vp)

/*
 * Misc vnode macros.
 */
#define	MANDMODE(mode)		(0)
#define	MANDLOCK(vp, mode)	(0)
#define	chklock(vp, op, offset, size, mode, ct)	(0)
#define	cleanlocks(vp, pid, foo)	do { } while (0)
#define	cleanshares(vp, pid)		do { } while (0)
#define	MODEMASK	07777		/* mode bits plus permission bits */
#define	PERMMASK	00777		/* permission bits */

/*
 * Flags to VOP_SETATTR/VOP_GETATTR.
 */
#define	ATTR_UTIME	0x01
#define	ATTR_EXEC	0x02
#define	ATTR_COMM	0x04
#define	ATTR_HINT	0x08
#define	ATTR_REAL	0x10
#define	ATTR_NOACLCHECK	0x20
#define	ATTR_TRIGGER	0x40

/*
 * Lookup/readdir flags.
 */
#define	LOOKUP_DIR		0x01
#define	LOOKUP_XATTR		0x02
#define	CREATE_XATTR_DIR	0x04
#define	LOOKUP_HAVE_SYSATTR_DIR	0x08

#define	V_RDDIR_ENTFLAGS	0x01
#define	V_RDDIR_ACCFILTER	0x02

/*
 * vn_rele_async declaration.
 */
struct taskq;
#ifdef _KERNEL
extern void vn_rele_async(struct vnode *vp, struct taskq *taskq);
#endif

#endif /* _SPL_OSV_VNODE_H */
