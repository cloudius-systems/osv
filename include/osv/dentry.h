/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_DENTRY_H
#define _OSV_DENTRY_H 1

#include <osv/mutex.h>
#include <bsd/sys/sys/queue.h>

struct vnode;

struct dentry {
	LIST_ENTRY(dentry) d_link;	/* link for hash list */
	int		d_refcnt;	/* reference count */
	char		*d_path;	/* pointer to path in fs */
	struct vnode	*d_vnode;
	struct mount	*d_mount;
};

#endif /* _OSV_DENTRY_H */
