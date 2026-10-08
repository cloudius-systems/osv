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

/*
 * LOCK ORDERING RULE (whole VFS):
 *
 *     vnode lock (vn_lock)  ->  dentry_hash_lock
 *
 * and NEVER the reverse.  namei() establishes this direction: it holds
 * vn_lock(dvp) across dentry_lookup()/dentry_alloc() (vfs_lookup.cc), both of
 * which take dentry_hash_lock inside.  Therefore no code may call anything
 * that takes a vnode lock while holding dentry_hash_lock.
 *
 * Inverting it deadlocks: a namei() holding vn_lock and waiting for
 * dentry_hash_lock, against a thread holding dentry_hash_lock and waiting for
 * vn_lock, leaves neither able to proceed.  Reaching that state needs two
 * distinct dentries aliasing one vnode, which rename() can produce: sys_rename()
 * holds vn_lock(vp1) across the destination lookup and the following
 * dentry_move()/dentry_remove(), and vp1 may be any vnode type.
 *
 * dentry_hash_lock does not call into the vnode layer.  Keep it that way, and
 * do not allocate while holding it.  It does nest dentry->d_lock (via
 * dentry_children_remove()); that direction is consistent at every site, and
 * d_lock is never held while taking dentry_hash_lock.
 */
static mutex dentry_hash_lock;

#ifdef DEBUG_VFS
/*
 * Teeth for the ordering rule above: vn_lock() asserts that the caller does not
 * already hold dentry_hash_lock.  Note that DEBUG_VFS is not enabled in a
 * default build (see vfs.h), so this only has effect when it is turned on
 * deliberately.
 */
bool vfs_dentry_hash_lock_held(void)
{
    return dentry_hash_lock.owned();
}
#endif

/*
 * dentry_hash_lock accessors.  Use these, not mutex_lock/unlock directly, so a
 * new call site cannot bypass the ordering rule above.
 */
static void dentry_hash_lock_acquire(void)
{
    mutex_lock(&dentry_hash_lock);
}

static void dentry_hash_lock_release(void)
{
    mutex_unlock(&dentry_hash_lock);
}

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
        return nullptr;
    }

    vref(vp);

    dp->d_refcnt = 1;
    dp->d_vnode = vp;
    dp->d_mount = mp;
    dp->d_path = strdup(path);
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

    dentry_hash_lock_acquire();
    LIST_INSERT_HEAD(&dentry_hash_table[dentry_hash(mp, path)], dp, d_link);
    dentry_hash_lock_release();
    return dp;
};

struct dentry *
dentry_lookup(struct mount *mp, char *path)
{
    struct dentry *dp;

    dentry_hash_lock_acquire();
    LIST_FOREACH(dp, &dentry_hash_table[dentry_hash(mp, path)], d_link) {
        if (dp->d_mount == mp && !strncmp(dp->d_path, path, PATH_MAX)) {
            dp->d_refcnt++;
            dentry_hash_lock_release();
            return dp;
        }
    }
    dentry_hash_lock_release();
    return nullptr;                /* not found */
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
    // Duplicate the new path BEFORE taking dentry_hash_lock.  strdup() can
    // block in the page allocator, and dentry_hash_lock serialises every name
    // lookup in the system; sleeping under it stalls all VFS name resolution
    // for the duration.  Nothing here needs the lock.
    char *new_path = strdup(path);

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
        // Update dp with the path duplicated above, outside the lock.
        dp->d_path = new_path;
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
    dentry_hash_lock_acquire();
    LIST_REMOVE(dp, d_link);
    /* put it on a fake list for drele() to work*/
    LIST_INSERT_HEAD(&fake, dp, d_link);
    dentry_hash_lock_release();
}

void
dref(struct dentry *dp)
{
    ASSERT(dp);
    ASSERT(dp->d_refcnt > 0);

    dentry_hash_lock_acquire();
    dp->d_refcnt++;
    dentry_hash_lock_release();
}

void
drele(struct dentry *dp)
{
    ASSERT(dp);
    ASSERT(dp->d_refcnt > 0);

    dentry_hash_lock_acquire();
    if (--dp->d_refcnt) {
        dentry_hash_lock_release();
        return;
    }
    /*
     * Last reference.  Unlink from the hash chain while still holding the
     * lock -- that is what makes the drop below safe: once dp is off the
     * chain, dentry_lookup() can no longer find it, so no other thread can
     * resurrect it by taking a new reference.  This thread is the sole owner
     * of dp from here on, and d_refcnt is 0 and stays 0.
     *
     * vn_del_name() must NOT be called under dentry_hash_lock: it takes the
     * vnode lock, which inverts the vn_lock -> dentry_hash_lock order that
     * namei() establishes (see the ordering note at the top of this file).
     * Release first, then touch the vnode.
     */
    LIST_REMOVE(dp, d_link);
    dentry_hash_lock_release();

    vn_del_name(dp->d_vnode, dp);

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
