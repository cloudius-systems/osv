/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
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
	struct dentry   *d_parent; /* pointer to parent */
	LIST_ENTRY(dentry) d_names_link; /* link fo vnode::d_names */
};

#ifdef __cplusplus

#include <boost/intrusive_ptr.hpp>

using dentry_ref = boost::intrusive_ptr<dentry>;

extern "C" {
    void dref(struct dentry* dp);
    void drele(struct dentry* dp);
};

inline void intrusive_ptr_add_ref(dentry* dp) { dref(dp); }
inline void intrusive_ptr_release(dentry* dp) { drele(dp); }

#endif

#endif /* _OSV_DENTRY_H */
