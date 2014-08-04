/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
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

#include <string.h>
#include <stdlib.h>
#include <sys/param.h>

#include <osv/dentry.h>
#include <osv/vnode.h>
#include "vfs.h"

#define DENTRY_BUCKETS 32

static LIST_HEAD(dentry_hash_head, dentry) dentry_hash_table[DENTRY_BUCKETS];
static LIST_HEAD(fake, dentry) fake;
static mutex dentry_hash_lock;

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
    struct dentry *dp = (dentry*)calloc(sizeof(*dp), 1);

    if (!dp) {
        return NULL;
    }

    vref(vp);

    dp->d_refcnt = 1;
    dp->d_vnode = vp;
    dp->d_mount = mp;
    dp->d_path = strdup(path);
    mutex_init(&dp->d_lock);
    LIST_INIT(&dp->d_children);

    if (parent_dp) {
        dref(parent_dp);
        WITH_LOCK(parent_dp->d_lock) {
            // Insert dp into its parent's children list.
            LIST_INSERT_HEAD(&parent_dp->d_children, dp, d_children_link);
        }
    }
    dp->d_parent = parent_dp;

    vn_add_name(vp, dp);

    mutex_lock(&dentry_hash_lock);
    LIST_INSERT_HEAD(&dentry_hash_table[dentry_hash(mp, path)], dp, d_link);
    mutex_unlock(&dentry_hash_lock);
    return dp;
};

struct dentry *
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

static void dentry_children_remove(struct dentry *dp)
{
    struct dentry *entry = nullptr;

    WITH_LOCK(dp->d_lock) {
        LIST_FOREACH(entry, &dp->d_children, d_children_link) {
            ASSERT(entry);
            ASSERT(entry->d_refcnt > 0);
            LIST_REMOVE(entry, d_link);
        }
    }
}

void
dentry_move(struct dentry *dp, struct dentry *parent_dp, char *path)
{
    struct dentry *old_pdp = dp->d_parent;
    char *old_path = dp->d_path;

    if (old_pdp) {
        WITH_LOCK(old_pdp->d_lock) {
            // Remove dp from its old parent's children list.
            LIST_REMOVE(dp, d_children_link);
        }
    }

    if (parent_dp) {
        dref(parent_dp);
        WITH_LOCK(parent_dp->d_lock) {
            // Insert dp into its new parent's children list.
            LIST_INSERT_HEAD(&parent_dp->d_children, dp, d_children_link);
        }
    }

    WITH_LOCK(dentry_hash_lock) {
        // Remove all dp's child dentries from the hashtable.
        dentry_children_remove(dp);
        // Remove dp with outdated hash info from the hashtable.
        LIST_REMOVE(dp, d_link);
        // Update dp.
        dp->d_path = strdup(path);
        dp->d_parent = parent_dp;
        // Insert dp updated hash info into the hashtable.
        LIST_INSERT_HEAD(&dentry_hash_table[dentry_hash(dp->d_mount, path)],
            dp, d_link);
    }

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
        WITH_LOCK(dp->d_parent->d_lock) {
            // Remove dp from its parent's children list.
            LIST_REMOVE(dp, d_children_link);
        }
        drele(dp->d_parent);
    }

    vrele(dp->d_vnode);

    free(dp->d_path);
    free(dp);
}

void
dentry_init(void)
{
    int i;

    for (i = 0; i < DENTRY_BUCKETS; i++) {
        LIST_INIT(&dentry_hash_table[i]);
    }
}
