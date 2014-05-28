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
static LIST_HEAD(fake, dentry) fake;
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
        while (*path) {
            val = ((val << 5) + val) + *path++;
        }
    }
    return (val ^ (unsigned long) mp) & (DENTRY_BUCKETS - 1);
}


struct dentry *
dentry_alloc(struct dentry *parent_dp, struct vnode *vp, const char *path)
{
    struct mount *mp = vp->v_mount;
    struct dentry *dp = calloc(sizeof(*dp), 1);

    if (!dp) {
        return NULL;
    }

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
        if (dp->d_mount == mp && !strncmp(dp->d_path, path, PATH_MAX)) {
            dp->d_refcnt++;
            mutex_unlock(&dentry_hash_lock);
            return dp;
        }
    }
    mutex_unlock(&dentry_hash_lock);
    return NULL;                /* not found */
}

void
dentry_move(struct dentry *dp, struct dentry *parent_dp, char *path)
{
    struct dentry *old_pdp = dp->d_parent;
    char *old_path = dp->d_path;

    if (parent_dp) {
        dref(parent_dp);
    }
    mutex_lock(&dentry_hash_lock);
    LIST_REMOVE(dp, d_link);
    dp->d_path = strdup(path);
    dp->d_parent = parent_dp;
    LIST_INSERT_HEAD(&dentry_hash_table[dentry_hash(dp->d_mount, path)], dp, d_link);
    mutex_unlock(&dentry_hash_lock);

    if (old_pdp) {
        drele(old_pdp);
    }

    free(old_path);
}

void
dentry_remove(struct dentry *dp)
{
    mutex_lock(&dentry_hash_lock);
    LIST_REMOVE(dp, d_link);
    /* put it on a fake list for drele() to work*/
    LIST_INSERT_HEAD(&fake, dp, d_link);
    mutex_unlock(&dentry_hash_lock);
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

static inline size_t read_link(struct vnode *vp, char *buf, size_t size)
{
    struct iovec iov = {
        .iov_base = buf,
        .iov_len  = size,
    };

    struct uio uio = {
        .uio_iov	= &iov,
        .uio_iovcnt	= 1,
        .uio_offset	= 0,
        .uio_resid	= size,
        .uio_rw		= UIO_READ,
    };

    return (VOP_READLINK(vp, &uio));
}

int
__namei(char *path, struct dentry **dpp, int flag)
{
    char *p;
    char node[PATH_MAX];
    char name[PATH_MAX];
    char fp[PATH_MAX];
    struct mount *mp;
    struct dentry *dp, *ddp;
    struct vnode *dvp, *vp;
    int error, i;
    int follow;

    DPRINTF(VFSDB_VNODE, ("namei: path=%s\n", path));

    follow = 0;
    if ((flag & AT_SYMLINK_FOLLOW) != 0) {
        follow = 1;
    }

start:
    /*
     * Convert a full path name to its mount point and
     * the local node in the file system.
     */
    if (vfs_findroot(path, &mp, &p)) {
        return ENOTDIR;
    }
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
    if (!ddp) {
        sys_panic("VFS: no root");
    }
    dref(ddp);

    node[0] = '\0';

    while (*p != '\0') {
        /*
         * Get lower directory/file name.
         */
        while (*p == '/') {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        for (i = 0; i < PATH_MAX; i++) {
            if (*p == '\0' || *p == '/') {
                break;
            }
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

        if (dp->d_vnode->v_type == VLNK && follow == 1) {
            char t[PATH_MAX];
            char l[PATH_MAX];

            memset(l, 0, sizeof(l));
            error = read_link(dp->d_vnode, l, sizeof(l));
            if (error != 0) {
                drele(dp);
                return (error);
            }

            if (l[0] == '/') {
                /*
                 * p might be pointing to fp already, use another
                 * temporary buffer t
                 */
                strlcpy(t, l, sizeof(t));
                strlcat(t, p, sizeof(t));
                strlcpy(fp, t, sizeof(fp));
            } else {
                strlcpy(t, node, strlen(node) - strlen(name));
                path_conv(t, l, fp);
            }

            drele(dp);
            path	= fp;
            dp		= NULL;
            ddp		= NULL;
            vp		= NULL;
            dvp		= NULL;
            name[0]	= 0;
            node[0]	= 0;
            goto start;
        }

        if (*p == '/' && ddp->d_vnode->v_type != VDIR) {
            drele(ddp);
            return ENOTDIR;
        }
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
 * Convert a pathname into a pointer to a dentry
 *
 * @path: full path name.
 * @dpp:  dentry to be returned.
 */
int
namei(char *path, struct dentry **dpp)
{
    return (__namei(path, dpp, AT_SYMLINK_FOLLOW));
}


int namei_nofollow(char *path, struct dentry **dpp)
{
    return (__namei(path, dpp, AT_SYMLINK_NOFOLLOW));
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
    if (!buf[0]) {
        return ENOTDIR;
    }
    if (file == buf) {
        dir = root;
    } else {
        *file = '\0';
        dir = buf;
    }
    /*
     * Get the vnode for directory
     */
    if ((error = namei(dir, &dp)) != 0) {
        return error;
    }
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

    for (i = 0; i < DENTRY_BUCKETS; i++) {
        LIST_INIT(&dentry_hash_table[i]);
    }
}
