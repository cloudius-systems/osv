// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * ARC (Adaptive Replacement Cache) OS-specific functions for OSv.
 *
 * These functions provide memory information to the ARC so it can
 * size itself appropriately and respond to memory pressure.
 */

#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/spa_impl.h>
#include <sys/zfs_context.h>
#include <sys/arc.h>
#include <sys/arc_os.h>
#include <sys/arc_impl.h>
#include <sys/vdev.h>
#include <sys/dsl_pool.h>
#include <sys/abd.h>
#include <sys/callb.h>
#include <sys/kstat.h>
#include <sys/zthr.h>
#include <sys/aggsum.h>

extern unsigned long physmem;
extern unsigned long freemem;

/*
 * osv_free_pages() is implemented in core/pagecache.cc (C++ side) and
 * returns the current number of free physical pages from the OSv allocator.
 * We call it periodically to update the global freemem so the ARC can
 * respond to sustained memory pressure while avoiding over-eager eviction
 * that would destabilise mmap-backed pools.
 */
extern unsigned long osv_free_pages(void);

uint_t zfs_arc_free_target = 0;

/*
 * Return how much memory is available for the ARC to use.
 * Positive values mean memory is available; negative means under pressure.
 *
 * We gently refresh freemem from the OSv page allocator on each call so
 * the ARC sees real trends in memory usage.  To avoid unstable feedback
 * (the ARC's own eviction transiently reduces freemem to near-zero just
 * before new pages are handed back to the allocator), we use a 7/8 low-pass
 * filter: freemem moves at most 1/8 of the measured distance per call.
 * This is enough to track the true free-page count over tens of seconds
 * (the timescale of inter-config transitions) while dampening sub-second
 * spikes that would otherwise trigger runaway eviction.
 */
int64_t
arc_available_memory(void)
{
	unsigned long measured = osv_free_pages();

	/*
	 * Low-pass update: new_freemem = (7*old + measured) / 8
	 * Initialised to physmem/4 in zfs_initialize_osv.c; first call
	 * pulls it closer to the real value without a step change.
	 */
	freemem = (freemem * 7 + measured) / 8;

	if (zfs_arc_free_target == 0)
		zfs_arc_free_target = (uint_t)(physmem / 64);

	return ((int64_t)PAGESIZE *
	    ((int64_t)freemem - zfs_arc_free_target));
}

/*
 * Return a default max arc size based on the amount of physical memory.
 *
 * OSv targets lightweight VMs that commonly run with 128–512 MiB RAM.
 * The FreeBSD formula (5/8 of RAM for < 1 GiB) is too aggressive: with
 * 128 MiB it claims 80 MiB for the ARC alone, leaving only 48 MiB for the
 * OSv kernel, pagecache, thread stacks, and virtio-blk DMA buffers - not
 * enough to run a database workload without hitting physical memory limits.
 *
 * Tiered formula:
 *   < 128 MiB  → 1/8 of RAM  (e.g. 16 MiB of 128 MiB)
 *   128–256 MiB→ 1/6 of RAM  (e.g. 21 MiB of 128 MiB reported by firmware)
 *   256–1 GiB  → 3/8 of RAM  (e.g. 96 MiB of 256 MiB)
 *   ≥ 1 GiB    → max(5/8 RAM, RAM − 1 GiB)  (same as FreeBSD)
 *
 * The sub-256 MiB tiers use a smaller fraction than the original 1/4 because:
 *   1. OSv runs ZFS alongside kernel, libs, stacks, and virtio-blk DMA buffers
 *      all sharing the same 127–128 MiB of RAM.
 *   2. When multiple ZFS datasets are mounted sequentially (e.g. benchmark
 *      configs), the ARC retains metadata from previous pools/datasets until
 *      its eviction thread catches up.  Leaving 7/8 (≥ 112 MiB) free for
 *      everything else prevents the peak-usage spike from exceeding physmem.
 */
uint64_t
arc_default_max(uint64_t min, uint64_t allmem)
{
	uint64_t size;

	if (allmem >= (1ULL << 30))
		size = MAX(allmem * 5 / 8, allmem - (1ULL << 30));
	else if (allmem >= (256ULL << 20))
		size = allmem * 3 / 8;
	else if (allmem >= (128ULL << 20))
		size = allmem / 8;
	else
		size = allmem / 8;

	return (MAX(size, min));
}

/*
 * Return total physical memory in bytes.
 */
uint64_t
arc_all_memory(void)
{
	return (ptob(physmem));
}

/*
 * Memory throttle check.
 * Returns non-zero if writes should be throttled due to memory pressure.
 * On OSv we don't throttle -- the ARC's own eviction handles pressure.
 */
int
arc_memory_throttle(spa_t *spa, uint64_t reserve, uint64_t txg)
{
	(void) spa;
	(void) reserve;
	(void) txg;
	return (0);
}

/*
 * Return free memory in bytes.
 */
uint64_t
arc_free_memory(void)
{
	return (ptob(osv_free_pages()));
}

/*
 * Low-memory event handler initialization.
 * OSv does not have FreeBSD's EVENTHANDLER(vm_lowmem) mechanism.
 * The ARC's own background eviction thread handles memory pressure.
 */
void
arc_lowmem_init(void)
{
}

void
arc_lowmem_fini(void)
{
}

/*
 * Memory hotplug notification (not applicable to OSv).
 */
void
arc_register_hotplug(void)
{
}

void
arc_unregister_hotplug(void)
{
}
