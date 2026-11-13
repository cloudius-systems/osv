/*-
 * Copyright (c) 2006-2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/mutex.h>

#include <bsd/porting/netport.h>
#include <assert.h>

#include <osv/export.h>

#include <osv/kernel_config_fork.h>
#if CONF_fork
/*
 * Fork COW-coherence for the ZFS (SPL) heap.
 *
 * OSv gives a forked child its own copy-on-write address space; APPLICATION
 * heap allocations made on an app thread land in the per-AS COW "fork arena"
 * (VA 0x3000..), which diverges physically per child.  ZFS, however, is a
 * KERNEL subsystem whose objects are touched from BOTH sides of the fork COW
 * boundary: an app thread calling in from a fork child's AS (e.g. PostgreSQL's
 * forked startup process running recovery/checkpoint) AND ZFS's own kernel
 * threads / I/O-completion paths in AS0 (the block-completion thread,
 * txg_sync_thread, dp_sync_taskq, zil lwb writer).  A great many of those
 * objects embed a synchronization primitive on which one side BLOCKS and the
 * other SIGNALS across the AS boundary, or are simply dereferenced by an AS0
 * thread: zio_t.io_cv, zil_commit_waiter_t.zcw_cv, dmu_tx / dbuf / dnode /
 * objset / arc_buf_hdr and their embedded locks and condvars.
 *
 * If such an object sits in the COW fork arena, the two address spaces touch
 * DIFFERENT physical copies -- a lost wakeup (every vCPU idles, PostgreSQL
 * never reaches "ready to accept connections") or an AS0 fault on a stale
 * pointer.  (The wait_record is already made cross-AS coherent by
 * coherent_wait_record in condvar::wait; the block-I/O bio by alloc_bio(); the
 * remaining divergence is the ZFS objects these primitives are embedded in.)
 *
 * Fix: route ALL SPL/ZFS heap allocations onto the identity kernel heap
 * (mapped verbatim in every address space) so every ZFS object is coherent in
 * every fork address space.  ZFS objects are shared kernel state, not
 * per-process app state, so this is their correct home; it only forgoes COW
 * isolation for the ZFS heap (which must be AS-coherent anyway) and is a no-op
 * for allocations already made on AS0 kernel threads (those never route to the
 * arena).  Mirrors the fork_arena::kernel_heap_scope used in-kernel for thread
 * objects, wait_records, epoll containers, the block bio, etc.  free()
 * dispatches purely by address, so identity-heap ZFS objects free correctly
 * from any address space.
 */
extern void fork_kernel_heap_push(void);
extern void fork_kernel_heap_pop(void);
#endif

void *
zfs_kmem_alloc(size_t size, int kmflags)
{
#if CONF_fork
	fork_kernel_heap_push();
#endif
	void *ptr = malloc(size);
#if CONF_fork
	fork_kernel_heap_pop();
#endif
	if (ptr && (kmflags & M_ZERO))
		memset(ptr, 0, size);
	return ptr;
}

void
zfs_kmem_free(void *buf, size_t size)
{
	free(buf);
}

static int
kmem_std_constructor(void *mem, int size, void *private, int flags)
{
	struct kmem_cache *cache = private;

	return (cache->kc_constructor(mem, cache->kc_private, flags));
}

static void
kmem_std_destructor(void *mem, int size, void *private)
{
	struct kmem_cache *cache = private;

	cache->kc_destructor(mem, cache->kc_private);
}

kmem_cache_t *
kmem_cache_create(char *name, size_t bufsize, size_t align,
    int (*constructor)(void *, void *, int), void (*destructor)(void *, void *),
    void (*reclaim)(void *), void *private, vmem_t *vmp, int cflags)
{
	kmem_cache_t *cache;

	ASSERT(vmp == NULL);

	cache = kmem_alloc(sizeof(*cache), KM_SLEEP);
	strlcpy(cache->kc_name, name, sizeof(cache->kc_name));
	cache->kc_constructor = constructor;
	cache->kc_destructor = destructor;
	cache->kc_private = private;
	cache->kc_size = bufsize;
	cache->kc_align = align;

	return (cache);
}

void
kmem_cache_destroy(kmem_cache_t *cache)
{
	kmem_free(cache, sizeof(*cache));
}

void *
kmem_cache_alloc(kmem_cache_t *cache, int flags)
{
	void *p;
#if CONF_fork
	fork_kernel_heap_push();
#endif
	if (cache->kc_align) {
		int error = posix_memalign(&p, cache->kc_align, cache->kc_size);
		if (error)
			p = NULL;
	} else
		p = malloc(cache->kc_size);
	if (p && (flags & M_ZERO))
		memset(p, 0, cache->kc_size);
	if (p != NULL && cache->kc_constructor != NULL)
		kmem_std_constructor(p, cache->kc_size, cache, flags);
#if CONF_fork
	fork_kernel_heap_pop();
#endif
	return (p);
}

void
kmem_cache_free(kmem_cache_t *cache, void *buf)
{
	if (cache->kc_destructor != NULL)
		kmem_std_destructor(buf, cache->kc_size, cache);
	free(buf);
}

void
kmem_cache_reap_now(kmem_cache_t *cache)
{
}

void
kmem_reap(void)
{
}

int
kmem_debugging(void)
{
	return (0);
}

OSV_LIB_SOLARIS_API
uint64_t kmem_size(void)
{
	return physmem * PAGE_SIZE;
}
