// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2014 by Chunwei Chen. All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * OSv ABD (ARC Buffered Data) OS-specific implementation.
 * Based on the FreeBSD version, simplified for OSv.
 *
 * OSv does not have VM pages, so scatter ABDs use kmem-allocated
 * page-sized chunks. This matches the FreeBSD approach.
 */

#include <sys/abd_impl.h>
#include <sys/types.h>
#include <sys/zio.h>
#include <sys/zfs_context.h>
#include <sys/arc.h>

/*
 * OSv zero_region size - must be at least PAGE_SIZE.
 * Set to 64KB to match typical page sizes.
 */
#ifndef ZERO_REGION_SIZE
#define ZERO_REGION_SIZE (64 * 1024)
#endif

typedef struct abd_stats {
	kstat_named_t abdstat_struct_size;
	kstat_named_t abdstat_scatter_cnt;
	kstat_named_t abdstat_scatter_data_size;
	kstat_named_t abdstat_scatter_chunk_waste;
	kstat_named_t abdstat_linear_cnt;
	kstat_named_t abdstat_linear_data_size;
} abd_stats_t;

static abd_stats_t abd_stats = {
	{ "struct_size",		KSTAT_DATA_UINT64 },
	{ "scatter_cnt",		KSTAT_DATA_UINT64 },
	{ "scatter_data_size",		KSTAT_DATA_UINT64 },
	{ "scatter_chunk_waste",	KSTAT_DATA_UINT64 },
	{ "linear_cnt",			KSTAT_DATA_UINT64 },
	{ "linear_data_size",		KSTAT_DATA_UINT64 },
};

struct {
	wmsum_t abdstat_struct_size;
	wmsum_t abdstat_scatter_cnt;
	wmsum_t abdstat_scatter_data_size;
	wmsum_t abdstat_scatter_chunk_waste;
	wmsum_t abdstat_linear_cnt;
	wmsum_t abdstat_linear_data_size;
} abd_sums;

/*
 * On OSv, use linear ABDs for small allocations (< PAGE_SIZE).
 */
static size_t zfs_abd_scatter_min_size = PAGE_SIZE + 1;

kmem_cache_t *abd_chunk_cache;
static kstat_t *abd_ksp;

abd_t *abd_zero_scatter = NULL;

static uint_t
abd_chunkcnt_for_bytes(size_t size)
{
	return ((size + PAGE_MASK) >> PAGE_SHIFT);
}

static inline uint_t
abd_scatter_chunkcnt(abd_t *abd)
{
	ASSERT(!abd_is_linear(abd));
	return (abd_chunkcnt_for_bytes(
	    ABD_SCATTER(abd).abd_offset + abd->abd_size));
}

boolean_t
abd_size_alloc_linear(size_t size)
{
	return (!zfs_abd_scatter_enabled || size < zfs_abd_scatter_min_size);
}

void
abd_update_scatter_stats(abd_t *abd, abd_stats_op_t op)
{
	uint_t n;

	n = abd_scatter_chunkcnt(abd);
	ASSERT(op == ABDSTAT_INCR || op == ABDSTAT_DECR);
	int waste = (n << PAGE_SHIFT) - abd->abd_size;
	if (op == ABDSTAT_INCR) {
		ABDSTAT_BUMP(abdstat_scatter_cnt);
		ABDSTAT_INCR(abdstat_scatter_data_size, abd->abd_size);
		ABDSTAT_INCR(abdstat_scatter_chunk_waste, waste);
		arc_space_consume(waste, ARC_SPACE_ABD_CHUNK_WASTE);
	} else {
		ABDSTAT_BUMPDOWN(abdstat_scatter_cnt);
		ABDSTAT_INCR(abdstat_scatter_data_size, -(int)abd->abd_size);
		ABDSTAT_INCR(abdstat_scatter_chunk_waste, -waste);
		arc_space_return(waste, ARC_SPACE_ABD_CHUNK_WASTE);
	}
}

void
abd_update_linear_stats(abd_t *abd, abd_stats_op_t op)
{
	ASSERT(op == ABDSTAT_INCR || op == ABDSTAT_DECR);
	if (op == ABDSTAT_INCR) {
		ABDSTAT_BUMP(abdstat_linear_cnt);
		ABDSTAT_INCR(abdstat_linear_data_size, abd->abd_size);
	} else {
		ABDSTAT_BUMPDOWN(abdstat_linear_cnt);
		ABDSTAT_INCR(abdstat_linear_data_size, -(int)abd->abd_size);
	}
}

void
abd_verify_scatter(abd_t *abd)
{
	uint_t i, n;

	ASSERT(!abd_is_linear_page(abd));
	ASSERT3U(ABD_SCATTER(abd).abd_offset, <, PAGE_SIZE);
	n = abd_scatter_chunkcnt(abd);
	for (i = 0; i < n; i++) {
		ASSERT3P(ABD_SCATTER(abd).abd_chunks[i], !=, NULL);
	}
}

void
abd_alloc_chunks(abd_t *abd, size_t size)
{
	uint_t i, n;

	n = abd_chunkcnt_for_bytes(size);
	for (i = 0; i < n; i++) {
		ABD_SCATTER(abd).abd_chunks[i] =
		    kmem_cache_alloc(abd_chunk_cache, KM_PUSHPAGE);
	}
}

void
abd_free_chunks(abd_t *abd)
{
	uint_t i, n;

	if (!abd_is_from_pages(abd)) {
		n = abd_scatter_chunkcnt(abd);
		for (i = 0; i < n; i++) {
			kmem_cache_free(abd_chunk_cache,
			    ABD_SCATTER(abd).abd_chunks[i]);
		}
	}
}

abd_t *
abd_alloc_struct_impl(size_t size)
{
	uint_t chunkcnt = abd_chunkcnt_for_bytes(size);
	size_t abd_size = MAX(sizeof (abd_t),
	    offsetof(abd_t, abd_u.abd_scatter.abd_chunks[chunkcnt]));
	abd_t *abd = kmem_alloc(abd_size, KM_PUSHPAGE);
	ASSERT3P(abd, !=, NULL);
	ABDSTAT_INCR(abdstat_struct_size, abd_size);

	return (abd);
}

void
abd_free_struct_impl(abd_t *abd)
{
	uint_t chunkcnt = abd_is_linear(abd) || abd_is_gang(abd) ? 0 :
	    abd_scatter_chunkcnt(abd);
	ssize_t size = MAX(sizeof (abd_t),
	    offsetof(abd_t, abd_u.abd_scatter.abd_chunks[chunkcnt]));
	kmem_free(abd, size);
	ABDSTAT_INCR(abdstat_struct_size, -size);
}

/*
 * On OSv, `zero_region` would resolve to the zero_region() FUNCTION defined
 * in openzfs_osv_compat.c (a VMA address, < 0x400000000000 in OSv's memory
 * layout).  Using a VMA address as a DMA buffer source causes:
 *   Assertion failed: virt >= phys_mem (core/mmu.cc: virt_to_phys: 183)
 * because OSv's virt_to_phys() requires the pointer to be in the physical
 * memory area (>= 0x400000000000).
 *
 * Fix: allocate a proper zero-filled page from kmem on init.  kmem_zalloc()
 * returns heap memory in OSv's physical memory area, so virt_to_phys() works.
 */
static char *osv_zero_page = NULL;

_Static_assert(ZERO_REGION_SIZE >= PAGE_SIZE, "zero_region too small");
static void
abd_alloc_zero_scatter(void)
{
	uint_t i, n;

	osv_zero_page = kmem_zalloc(PAGE_SIZE, KM_SLEEP);

	n = abd_chunkcnt_for_bytes(SPA_MAXBLOCKSIZE);
	abd_zero_scatter = abd_alloc_struct(SPA_MAXBLOCKSIZE);
	abd_zero_scatter->abd_flags |= ABD_FLAG_OWNER;
	abd_zero_scatter->abd_size = SPA_MAXBLOCKSIZE;

	ABD_SCATTER(abd_zero_scatter).abd_offset = 0;

	for (i = 0; i < n; i++) {
		ABD_SCATTER(abd_zero_scatter).abd_chunks[i] = osv_zero_page;
	}

	ABDSTAT_BUMP(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, PAGE_SIZE);
}

static void
abd_free_zero_scatter(void)
{
	ABDSTAT_BUMPDOWN(abdstat_scatter_cnt);
	ABDSTAT_INCR(abdstat_scatter_data_size, -(int)PAGE_SIZE);

	abd_free_struct(abd_zero_scatter);
	abd_zero_scatter = NULL;

	kmem_free(osv_zero_page, PAGE_SIZE);
	osv_zero_page = NULL;
}

static int
abd_kstats_update(kstat_t *ksp, int rw)
{
	abd_stats_t *as = ksp->ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);
	as->abdstat_struct_size.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_struct_size);
	as->abdstat_scatter_cnt.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_cnt);
	as->abdstat_scatter_data_size.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_data_size);
	as->abdstat_scatter_chunk_waste.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_scatter_chunk_waste);
	as->abdstat_linear_cnt.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_linear_cnt);
	as->abdstat_linear_data_size.value.ui64 =
	    wmsum_value(&abd_sums.abdstat_linear_data_size);
	return (0);
}

void
abd_init(void)
{
	abd_chunk_cache = kmem_cache_create("abd_chunk", PAGE_SIZE, 0,
	    NULL, NULL, NULL, NULL, 0, KMC_NODEBUG | KMC_RECLAIMABLE);

	wmsum_init(&abd_sums.abdstat_struct_size, 0);
	wmsum_init(&abd_sums.abdstat_scatter_cnt, 0);
	wmsum_init(&abd_sums.abdstat_scatter_data_size, 0);
	wmsum_init(&abd_sums.abdstat_scatter_chunk_waste, 0);
	wmsum_init(&abd_sums.abdstat_linear_cnt, 0);
	wmsum_init(&abd_sums.abdstat_linear_data_size, 0);

	abd_ksp = kstat_create("zfs", 0, "abdstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (abd_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (abd_ksp != NULL) {
		abd_ksp->ks_data = &abd_stats;
		abd_ksp->ks_update = abd_kstats_update;
		kstat_install(abd_ksp);
	}

	abd_alloc_zero_scatter();
}

void
abd_fini(void)
{
	abd_free_zero_scatter();

	if (abd_ksp != NULL) {
		kstat_delete(abd_ksp);
		abd_ksp = NULL;
	}

	wmsum_fini(&abd_sums.abdstat_struct_size);
	wmsum_fini(&abd_sums.abdstat_scatter_cnt);
	wmsum_fini(&abd_sums.abdstat_scatter_data_size);
	wmsum_fini(&abd_sums.abdstat_scatter_chunk_waste);
	wmsum_fini(&abd_sums.abdstat_linear_cnt);
	wmsum_fini(&abd_sums.abdstat_linear_data_size);

	kmem_cache_destroy(abd_chunk_cache);
	abd_chunk_cache = NULL;
}

void
abd_free_linear_page(abd_t *abd)
{
	/*
	 * OSv does not use VM pages for linear ABDs.
	 * This should not be called, but handle gracefully.
	 */
	(void) abd;
	panic("abd_free_linear_page: not supported on OSv");
}

/*
 * For block I/O, use linear ABDs on OSv since we don't have
 * scatter/gather DMA support.
 */
abd_t *
abd_alloc_for_io(size_t size, boolean_t is_metadata)
{
	return (abd_alloc_linear(size, is_metadata));
}

abd_t *
abd_get_offset_scatter(abd_t *abd, abd_t *sabd, size_t off,
    size_t size)
{
	abd_verify(sabd);
	ASSERT3U(off, <=, sabd->abd_size);

	size_t new_offset = ABD_SCATTER(sabd).abd_offset + off;
	size_t chunkcnt = abd_chunkcnt_for_bytes(
	    (new_offset & PAGE_MASK) + size);

	ASSERT3U(chunkcnt, <=, abd_scatter_chunkcnt(sabd));

	if (abd != NULL &&
	    offsetof(abd_t, abd_u.abd_scatter.abd_chunks[chunkcnt]) >
	    sizeof (abd_t)) {
		abd = NULL;
	}

	if (abd == NULL)
		abd = abd_alloc_struct(chunkcnt << PAGE_SHIFT);

	ABD_SCATTER(abd).abd_offset = new_offset & PAGE_MASK;

	/* Copy the scatterlist starting at the correct offset */
	(void) memcpy(&ABD_SCATTER(abd).abd_chunks,
	    &ABD_SCATTER(sabd).abd_chunks[new_offset >> PAGE_SHIFT],
	    chunkcnt * sizeof (void *));

	return (abd);
}

/*
 * Initialize the abd_iter.
 */
void
abd_iter_init(struct abd_iter *aiter, abd_t *abd)
{
	ASSERT(!abd_is_gang(abd));
	abd_verify(abd);
	memset(aiter, 0, sizeof (struct abd_iter));
	aiter->iter_abd = abd;
}

boolean_t
abd_iter_at_end(struct abd_iter *aiter)
{
	return (aiter->iter_pos == aiter->iter_abd->abd_size);
}

void
abd_iter_advance(struct abd_iter *aiter, size_t amount)
{
	ASSERT3P(aiter->iter_mapaddr, ==, NULL);
	ASSERT0(aiter->iter_mapsize);

	if (abd_iter_at_end(aiter))
		return;

	aiter->iter_pos += amount;
}

void
abd_iter_map(struct abd_iter *aiter)
{
	void *paddr;

	ASSERT3P(aiter->iter_mapaddr, ==, NULL);
	ASSERT0(aiter->iter_mapsize);

	if (abd_iter_at_end(aiter))
		return;

	abd_t *abd = aiter->iter_abd;
	size_t offset = aiter->iter_pos;
	if (abd_is_linear(abd)) {
		aiter->iter_mapsize = abd->abd_size - offset;
		paddr = ABD_LINEAR_BUF(abd);
	} else {
		offset += ABD_SCATTER(abd).abd_offset;
		paddr = ABD_SCATTER(abd).abd_chunks[offset >> PAGE_SHIFT];
		offset &= PAGE_MASK;
		aiter->iter_mapsize = MIN(PAGE_SIZE - offset,
		    abd->abd_size - aiter->iter_pos);
	}
	aiter->iter_mapaddr = (char *)paddr + offset;
}

void
abd_iter_unmap(struct abd_iter *aiter)
{
	if (!abd_iter_at_end(aiter)) {
		ASSERT3P(aiter->iter_mapaddr, !=, NULL);
		ASSERT3U(aiter->iter_mapsize, >, 0);
	}

	aiter->iter_mapaddr = NULL;
	aiter->iter_mapsize = 0;
}

void
abd_cache_reap_now(void)
{
	kmem_cache_reap_soon(abd_chunk_cache);
}

/*
 * Allocate a scatter ABD wrapping caller-owned pages.
 *
 * On OSv, vm_page_t is void* - the virtual address of the start of a
 * PAGE_SIZE-aligned region.  No page pinning is required because OSv is a
 * single-address-space unikernel: kernel and "user" share the same VA space.
 *
 * Parameters:
 *   pages  - array of page base addresses; each element is the start of a
 *            PAGE_SIZE region owned by the caller (e.g. an IOV buffer page).
 *   offset - byte offset within pages[0] where the data begins (0 for
 *            page-aligned Direct I/O, which is the common case after
 *            zfs_uio_page_aligned() has been checked).
 *   size   - number of data bytes covered by this ABD.
 *
 * The returned ABD has ABD_FLAG_FROM_PAGES set, which tells abd_free_chunks()
 * to skip freeing the chunk pointers (since the pages belong to the caller),
 * and ABD_FLAG_OWNER set so that abd_free() will call abd_free_scatter()
 * and ultimately abd_free_struct() to release the ABD struct itself.
 */
abd_t *
abd_alloc_from_pages(vm_page_t *pages, unsigned long offset, uint64_t size)
{
	ASSERT3U(offset, <, PAGE_SIZE);
	ASSERT3U(size, >, 0);

	/*
	 * abd_alloc_struct(n) allocates an ABD struct with chunk-pointer space
	 * for ceil(n / PAGE_SIZE) entries.  Passing (offset + size) gives us
	 * exactly the right number of page slots.
	 */
	abd_t *abd = abd_alloc_struct(offset + size);

	/*
	 * FROM_PAGES: chunk memory is caller-owned; abd_free_chunks() will not
	 *             call kmem_cache_free() on these pointers.
	 * OWNER:      abd_free() calls abd_free_scatter() → abd_free_chunks()
	 *             (which is a no-op for FROM_PAGES chunks), then frees the
	 *             ABD struct because ABD_FLAG_ALLOCD was set by
	 *             abd_alloc_struct().
	 */
	abd->abd_flags |= ABD_FLAG_FROM_PAGES | ABD_FLAG_OWNER;
	abd->abd_size = size;
	ABD_SCATTER(abd).abd_offset = offset;

	uint_t chunkcnt = abd_chunkcnt_for_bytes(offset + size);
	for (uint_t i = 0; i < chunkcnt; i++)
		ABD_SCATTER(abd).abd_chunks[i] = pages[i];

	abd_update_scatter_stats(abd, ABDSTAT_INCR);
	return (abd);
}

void *
abd_borrow_buf(abd_t *abd, size_t n)
{
	void *buf;
	abd_verify(abd);
	ASSERT3U(abd->abd_size, >=, 0);
	if (abd_is_linear(abd)) {
		buf = abd_to_buf(abd);
	} else {
		buf = zio_buf_alloc(n);
	}
#ifdef ZFS_DEBUG
	(void) zfs_refcount_add_many(&abd->abd_children, n, buf);
#endif
	return (buf);
}

void *
abd_borrow_buf_copy(abd_t *abd, size_t n)
{
	void *buf = abd_borrow_buf(abd, n);
	if (!abd_is_linear(abd)) {
		abd_copy_to_buf(buf, abd, n);
	}
	return (buf);
}

void
abd_return_buf(abd_t *abd, void *buf, size_t n)
{
	abd_verify(abd);
	ASSERT3U(abd->abd_size, >=, n);
#ifdef ZFS_DEBUG
	(void) zfs_refcount_remove_many(&abd->abd_children, n, buf);
#endif
	if (abd_is_linear(abd)) {
		ASSERT3P(buf, ==, abd_to_buf(abd));
	} else if (abd_is_gang(abd)) {
		zio_buf_free(buf, n);
	} else {
		ASSERT0(abd_cmp_buf(abd, buf, n));
		zio_buf_free(buf, n);
	}
}

void
abd_return_buf_copy(abd_t *abd, void *buf, size_t n)
{
	if (!abd_is_linear(abd)) {
		abd_copy_from_buf(abd, buf, n);
	}
	abd_return_buf(abd, buf, n);
}
