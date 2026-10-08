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
#include <atomic>

#include <osv/dentry.h>
#include <osv/vnode.h>
#include <osv/debug.h>
#include "vfs.h"

#define DENTRY_BUCKETS 32

/*
 * The "fake" chain that dentry_remove() parks unlinked-but-still-referenced
 * dentries on is just one more chain, so give it a slot in the same array and
 * it gets a lock by the same rule as every other chain.
 */
#define DENTRY_FAKE_INDEX  DENTRY_BUCKETS
#define DENTRY_CHAINS      (DENTRY_BUCKETS + 1)

static LIST_HEAD(dentry_hash_head, dentry) dentry_hash_table[DENTRY_CHAINS];

/*
 * LOCK ORDERING RULE (whole VFS):
 *
 *     vnode lock (vn_lock)  ->  any dentry chain lock
 *
 * and NEVER the reverse.  namei() establishes this direction: it holds
 * vn_lock(dvp) across dentry_lookup()/dentry_alloc() (vfs_lookup.cc), both of
 * which take a chain lock inside.  Therefore no code may call anything that
 * takes a vnode lock while holding a chain lock.  Splitting one lock into
 * several does not change this: a per-chain lock inverts against vn_lock
 * exactly as the single global one did.
 *
 * drele() used to violate exactly that: it took the hash lock and then called
 * vn_del_name(), which does vn_lock() internally.  A namei() on one thread
 * (vn_lock held, waiting for the hash lock) against a drele() on another
 * (hash lock held, waiting for vn_lock) is an AB-BA deadlock with nothing
 * left runnable -- observed as all vCPUs halted in do_idle with an empty
 * wakeup mask and >1000 threads parked, while memory was plentiful.
 *
 * Chain locks are leaf locks.  Keep them that way: do not call out to vnode
 * code, and do not allocate, while holding one.
 *
 * MULTI-CHAIN OPERATIONS.  Most sites touch exactly one chain, which is the
 * point of the split.  Two do not, and both are handled explicitly rather
 * than assumed independent:
 *
 *   - dentry_move() unlinks dp from the chain for its old path, re-inserts it
 *     on the chain for the new path, and unlinks every child of dp from
 *     whatever chains they happen to be on.  It therefore takes ALL chain
 *     locks (dentry_lock_all), in ascending index order.
 *   - dentry_remove() moves a dentry from its hash chain to the fake chain,
 *     i.e. two chains, so it locks both, again in ascending index order.
 *
 * Ascending-index acquisition is what keeps chain-vs-chain lock order safe.
 * d_hash_index records which chain a dentry is currently on, so an unlink can
 * find the right lock without rehashing a d_path that a rename may already
 * have replaced.
 */
static mutex dentry_chain_lock[DENTRY_CHAINS];

/*
 * OSV_VFS_DENTRY_PERBUCKET=0 : route every chain lock to chain 0's mutex, so
 * all chains share one lock and the code behaves like the original single
 * global dentry_hash_lock.  Same-binary A/B for the lock split.  The ordering
 * fix in drele() is unconditional and is NOT affected by this switch.
 */
static bool dentry_perbucket_enabled()
{
    static std::atomic<int> cached{-1};
    int v = cached.load(std::memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("OSV_VFS_DENTRY_PERBUCKET");
        v = (!e || !e[0]) ? 1 : (e[0] != '0');
        // Proof of binding: say what the flag resolved to, once.  The default
        // is the non-inert value, so a dropped --env= would otherwise give
        // identical behaviour in both arms of an A/B and read as "no effect".
        debug_early("VFSFLAG OSV_VFS_DENTRY_PERBUCKET");
        debug_early(e ? "=" : " unset, default ");
        debug_early(v ? "1\n" : "0\n");
        cached.store(v, std::memory_order_relaxed);
    }
    return v != 0;
}

static mutex *chain_mutex(unsigned index)
{
    return &dentry_chain_lock[dentry_perbucket_enabled() ? index : 0];
}

#ifdef DEBUG_VFS
/*
 * Teeth for the ordering rule above.  When DEBUG_VFS is on, every thread
 * tracks how many dentry chain locks it holds; vn_lock() asserts that it
 * holds none.  This fires on the unfixed drele() and is silent once
 * vn_del_name() is moved out of the critical section.
 */
extern "C" bool vfs_dentry_hash_lock_held(void);   /* declared in vfs.h */
static __thread int dentry_hash_lock_depth;
bool vfs_dentry_hash_lock_held(void)
{
    return dentry_hash_lock_depth != 0;
}
static void dentry_hash_lock_enter(void) { dentry_hash_lock_depth++; }
static void dentry_hash_lock_exit(void)  { dentry_hash_lock_depth--; }
#else
static inline void dentry_hash_lock_enter(void) {}
static inline void dentry_hash_lock_exit(void)  {}
#endif

/*
 * Chain lock accessors.  Use these, not mutex_lock/unlock directly, so the
 * ordering instrumentation cannot be bypassed by a new call site.
 */
static void dentry_lock(unsigned index)
{
    mutex_lock(chain_mutex(index));
    dentry_hash_lock_enter();
}

static void dentry_unlock(unsigned index)
{
    dentry_hash_lock_exit();
    mutex_unlock(chain_mutex(index));
}

/*
 * Lock every chain, ascending.  Used only by dentry_move(), which can touch an
 * unbounded set of chains (dp's old chain, its new chain, and one per child).
 * With OSV_VFS_DENTRY_PERBUCKET=0 all indices alias chain 0, so take that
 * single lock once instead of recursing on it DENTRY_CHAINS times.
 *
 * ponytail: lock-all is the coarse option for a rare operation (rename of a
 * directory with cached children).  If rename ever shows up hot, narrow it to
 * the union of chains actually touched, collected under d_lock first.
 */
static void dentry_lock_all(void)
{
    if (!dentry_perbucket_enabled()) {
        dentry_lock(0);
        return;
    }
    for (unsigned i = 0; i < DENTRY_CHAINS; i++) {
        dentry_lock(i);
    }
}

static void dentry_unlock_all(void)
{
    if (!dentry_perbucket_enabled()) {
        dentry_unlock(0);
        return;
    }
    for (unsigned i = DENTRY_CHAINS; i > 0; i--) {
        dentry_unlock(i - 1);
    }
}

/*
 * Lock the chain a dentry is currently on, and return its index.
 *
 * d_hash_index must be read to know which lock to take, but it is itself only
 * stable under that lock, and dentry_move()/dentry_remove() can move a dentry
 * between chains.  So read, lock, then re-check: if the index changed while we
 * were acquiring, we locked the wrong chain -- drop it and retry.  Converges
 * because a dentry's chain only changes on rename/unlink, not in a loop.
 */
static unsigned dentry_lock_chain_of(struct dentry *dp)
{
    for (;;) {
        unsigned index = dp->d_hash_index;
        dentry_lock(index);
        if (dp->d_hash_index == index) {
            return index;
        }
        dentry_unlock(index);
    }
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

    unsigned index = dentry_hash(mp, path);
    dentry_lock(index);
    dp->d_hash_index = index;
    LIST_INSERT_HEAD(&dentry_hash_table[index], dp, d_link);
    dentry_unlock(index);
    return dp;
};

struct dentry *
dentry_lookup(struct mount *mp, char *path)
{
    struct dentry *dp;

    unsigned index = dentry_hash(mp, path);
    dentry_lock(index);
    LIST_FOREACH(dp, &dentry_hash_table[index], d_link) {
        if (dp->d_mount == mp && !strncmp(dp->d_path, path, PATH_MAX)) {
            dp->d_refcnt++;
            dentry_unlock(index);
            return dp;
        }
    }
    dentry_unlock(index);
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
    }}

void
dentry_move(struct dentry *dp, struct dentry *parent_dp, char *path)
{
    struct dentry *old_pdp = dp->d_parent;
    char *old_path = dp->d_path;
    // Duplicate the new path BEFORE taking dentry_hash_lock.  strdup() can
    // block in the page allocator, and dentry_hash_lock is a leaf lock held
    // by every lookup in the system; sleeping under it stalls all VFS name
    // resolution for the duration.  Nothing here needs the lock.
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

    // dentry_move touches dp's old chain, dp's new chain, and the chain of
    // every cached child, so it takes them all (ascending; see the note above).
    dentry_lock_all();
    // Remove all dp's child dentries from the hashtable.
    dentry_children_remove(dp);
    // Remove dp with outdated hash info from the hashtable.
    LIST_REMOVE(dp, d_link);
    // Update dp with the path duplicated above, outside the lock.
    dp->d_path = new_path;
    dp->d_parent = parent_dp;
    // Insert dp updated hash info into the hashtable.
    unsigned index = dentry_hash(dp->d_mount, path);
    dp->d_hash_index = index;
    LIST_INSERT_HEAD(&dentry_hash_table[index], dp, d_link);
    dentry_unlock_all();

    if (old_pdp) {
        drele(old_pdp);
    }

    free(old_path);
}

void
dentry_remove(struct dentry *dp)
{
    // Two chains: the one dp is on, and the fake chain it moves to.  Lock the
    // current chain first (re-checking the index), then the fake chain if it
    // is a different one, keeping ascending order.
    unsigned index = dentry_lock_chain_of(dp);
    bool need_fake = dentry_perbucket_enabled() && index != DENTRY_FAKE_INDEX;
    if (need_fake) {
        // DENTRY_FAKE_INDEX is the highest index, so taking it second is
        // already ascending order.
        dentry_lock(DENTRY_FAKE_INDEX);
    }
    LIST_REMOVE(dp, d_link);
    /* put it on a fake list for drele() to work*/
    LIST_INSERT_HEAD(&dentry_hash_table[DENTRY_FAKE_INDEX], dp, d_link);
    dp->d_hash_index = DENTRY_FAKE_INDEX;
    if (need_fake) {
        dentry_unlock(DENTRY_FAKE_INDEX);
    }
    dentry_unlock(index);
}

void
dref(struct dentry *dp)
{
    ASSERT(dp);
    ASSERT(dp->d_refcnt > 0);

    unsigned index = dentry_lock_chain_of(dp);
    dp->d_refcnt++;
    dentry_unlock(index);
}

void
drele(struct dentry *dp)
{
    ASSERT(dp);
    ASSERT(dp->d_refcnt > 0);

    unsigned index = dentry_lock_chain_of(dp);
    if (--dp->d_refcnt) {
        dentry_unlock(index);
        return;
    }
    /*
     * Last reference.  Unlink from the hash chain while still holding the
     * lock -- that is what makes the drop below safe: once dp is off the
     * chain, dentry_lookup() can no longer find it, so no other thread can
     * resurrect it by taking a new reference.  This thread is the sole owner
     * of dp from here on, and d_refcnt is 0 and stays 0.
     *
     * vn_del_name() must NOT be called under a chain lock: it takes the vnode
     * lock, which inverts the vn_lock -> chain lock order that namei()
     * establishes (see the ordering note at the top of this file).  Release
     * first, then touch the vnode.
     */
    LIST_REMOVE(dp, d_link);
    dentry_unlock(index);

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

    for (i = 0; i < DENTRY_CHAINS; i++) {
        LIST_INIT(&dentry_hash_table[i]);
    }
}
