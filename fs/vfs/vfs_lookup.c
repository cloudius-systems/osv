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

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <osv/dentry.h>
#include <osv/vnode.h>
#include "vfs.h"

#define DENTRY_BUCKETS 32

static LIST_HEAD(dentry_hash_head, dentry) dentry_hash_table[DENTRY_BUCKETS];
static struct mutex dentry_hash_lock;

/*
 * Get the hash value from the mount point and path name.
 * XXX: replace with a better hash for 64-bit pointers.
 */
static u_int
dentry_hash(struct mount *mp, const char *path)
{
	u_int val = 0;

	if (path) {
		while (*path)
			val = ((val << 5) + val) + *path++;
	}
	return (val ^ (unsigned long)mp) & (DENTRY_BUCKETS - 1);
}


struct dentry *
dentry_alloc(struct dentry *parent_dp, struct vnode *vp, const char *path)
{
	struct mount *mp = vp->v_mount;
	struct dentry *dp = calloc(sizeof(*dp), 1);

	if (!dp)
		return NULL;

	vp->v_refcnt++;

	dp->d_refcnt = 1;
	dp->d_vnode = vp;
	dp->d_mount = mp;
	dp->d_path = strdup(path);

	if (parent_dp) {
		dref(parent_dp);
	}
	dp->d_parent = parent_dp;

	vn_add_name(vp, dp);

	mutex_lock(&dentry_hash_lock);
	LIST_INSERT_HEAD(&dentry_hash_table[dentry_hash(mp, path)], dp, d_link);
	mutex_unlock(&dentry_hash_lock);
	return dp;
};

static struct dentry *
dentry_lookup(struct mount *mp, char *path)
{
	struct dentry *dp;

	mutex_lock(&dentry_hash_lock);
	LIST_FOREACH(dp, &dentry_hash_table[dentry_hash(mp, path)], d_link) {
		if (dp->d_mount == mp &&
		    !strncmp(dp->d_path, path, PATH_MAX)) {
			dp->d_refcnt++;
			mutex_unlock(&dentry_hash_lock);
			return dp;
		}
	}
	mutex_unlock(&dentry_hash_lock);
	return NULL;		/* not found */
}

void
dref(struct dentry *dp)
{
	ASSERT(dp);
	ASSERT(dp->d_refcnt > 0);

	mutex_lock(&dentry_hash_lock);
	dp->d_refcnt++;
	mutex_unlock(&dentry_hash_lock);
}

void
drele(struct dentry *dp)
{
	ASSERT(dp);
	ASSERT(dp->d_refcnt > 0);

	mutex_lock(&dentry_hash_lock);
	if (--dp->d_refcnt) {
		mutex_unlock(&dentry_hash_lock);
		return;
	}
	LIST_REMOVE(dp, d_link);
	vn_del_name(dp->d_vnode, dp);

	mutex_unlock(&dentry_hash_lock);

	if (dp->d_parent) {
		drele(dp->d_parent);
	}

	vrele(dp->d_vnode);

	free(dp->d_path);
	free(dp);
}

/*
 * Convert a pathname into a pointer to a dentry
 *
 * @path: full path name.
 * @dpp:  dentry to be returned.
 */
int
namei(char *path, struct dentry **dpp)
{
	char *p;
	char node[PATH_MAX];
	char name[PATH_MAX];
	struct mount *mp;
	struct dentry *dp, *ddp;
	struct vnode *dvp, *vp;
	int error, i;

	DPRINTF(VFSDB_VNODE, ("namei: path=%s\n", path));

	/*
	 * Convert a full path name to its mount point and
	 * the local node in the file system.
	 */
	if (vfs_findroot(path, &mp, &p))
		return ENOTDIR;
	strlcpy(node, "/", sizeof(node));
	strlcat(node, p, sizeof(node));
	dp = dentry_lookup(mp, node);
	if (dp) {
		/* vnode is already active. */
		*dpp = dp;
		return 0;
	}
	/*
	 * Find target vnode, started from root directory.
	 * This is done to attach the fs specific data to
	 * the target vnode.
	 */
	ddp = mp->m_root;
	if (!ddp)
		sys_panic("VFS: no root");
	dref(ddp);

	node[0] = '\0';

	while (*p != '\0') {
		/*
		 * Get lower directory/file name.
		 */
		while (*p == '/')
			p++;

		if (*p == '\0')
			break;

		for (i = 0; i < PATH_MAX; i++) {
			if (*p == '\0' || *p == '/')
				break;
			name[i] = *p++;
		}
		name[i] = '\0';

		/*
		 * Get a vnode for the target.
		 */
		strlcat(node, "/", sizeof(node));
		strlcat(node, name, sizeof(node));
		dvp = ddp->d_vnode;
		vn_lock(dvp);
		dp = dentry_lookup(mp, node);
		if (dp == NULL) {
			/* Find a vnode in this directory. */
			error = VOP_LOOKUP(dvp, name, &vp);
			if (error) {
				vn_unlock(dvp);
				drele(ddp);
				return error;
			}

			dp = dentry_alloc(ddp, vp, node);
			vput(vp);

			if (!dp) {
				vn_unlock(dvp);
				drele(ddp);
				return ENOMEM;
			}
		}
		vn_unlock(dvp);
		drele(ddp);
		ddp = dp;

		if (*p == '/' && ddp->d_vnode->v_type != VDIR) {
			drele(ddp);
			return ENOTDIR;
		}

		while (*p != '\0' && *p != '/')
			p++;
	}

#if 0
	/*
	 * Detemine X permission.
	 */
	if (vp->v_type != VDIR && sec_vnode_permission(path) != 0) {
		vp->v_mode &= ~(0111);
	}
#endif

	*dpp = dp;
	return 0;
}

/*
 * Search a pathname.
 * This is a very central but not so complicated routine. ;-P
 *
 * @path: full path.
 * @dpp:  pointer to dentry for directory.
 * @name: pointer to file name in path.
 *
 * This routine returns a locked directory vnode and file name.
 */
int
lookup(char *path, struct dentry **dpp, char **name)
{
	char buf[PATH_MAX];
	char root[] = "/";
	char *file, *dir;
	struct dentry *dp;
	int error;

	DPRINTF(VFSDB_VNODE, ("lookup: path=%s\n", path));

	/*
	 * Get the path for directory.
	 */
	strlcpy(buf, path, sizeof(buf));
	file = strrchr(buf, '/');
	if (!buf[0])
		return ENOTDIR;
	if (file == buf)
		dir = root;
	else {
		*file = '\0';
		dir = buf;
	}
	/*
	 * Get the vnode for directory
	 */
	if ((error = namei(dir, &dp)) != 0)
		return error;
	if (dp->d_vnode->v_type != VDIR) {
		drele(dp);
		return ENOTDIR;
	}

	*dpp = dp;

	/*
	 * Get the file name
	 */
	*name = strrchr(path, '/') + 1;
	return 0;
}

/*
 * vnode_init() is called once (from vfs_init)
 * in initialization.
 */
void
lookup_init(void)
{
	int i;

	for (i = 0; i < DENTRY_BUCKETS; i++)
		LIST_INIT(&dentry_hash_table[i]);
}
