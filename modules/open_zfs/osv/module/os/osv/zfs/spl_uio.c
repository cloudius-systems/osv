// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * OSv UIO operations for ZFS.
 */

#include <sys/uio.h>
#include <sys/zfs_context.h>

int
zfs_uiomove(void *cp, size_t n, zfs_uio_rw_t dir, zfs_uio_t *uio)
{
	ASSERT3U(zfs_uio_rw(uio), ==, dir);
	return (uiomove(cp, (int)n, GET_UIO_STRUCT(uio)));
}

int
zfs_uiocopy(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *uio,
    size_t *cbytes)
{
	struct uio uio_clone;
	int error;

	ASSERT3U(zfs_uio_rw(uio), ==, rw);

	/*
	 * Clone the uio for a NON-DESTRUCTIVE copy.  The struct uio is shallow
	 * copied first (iov pointer, iovcnt, offset, resid).  The iovec array
	 * MUST also be cloned: uiomove() advances iov_base and zeros iov_len
	 * on each iovec it touches, so if we reuse the caller's iov array, the
	 * caller is left with all-zero iov_len entries while uio_resid still
	 * reflects the un-consumed amount.  A subsequent zfs_uioskip() then
	 * walks off the end of the array (every iov it sees has iov_len == 0,
	 * triggering the "advance pointer; iovcnt--" branch repeatedly) and
	 * reads/scribbles on memory past the iov[] into uio_resid going wildly
	 * negative - which is exactly the corruption observed when cpiod
	 * writes 14 KiB files into ZFS and the resulting on-disk file has
	 * holes that read back as zeros.
	 *
	 * Allocate a private iovec array on the stack and copy each entry.
	 */
	int iovcnt = zfs_uio_iovcnt(uio);
	struct iovec iov_clone[iovcnt];
	for (int i = 0; i < iovcnt; i++) {
		iov_clone[i] = GET_UIO_STRUCT(uio)->uio_iov[i];
	}
	uio_clone = *(GET_UIO_STRUCT(uio));
	uio_clone.uio_iov = iov_clone;

	error = uiomove(p, n, &uio_clone);
	*cbytes = zfs_uio_resid(uio) - uio_clone.uio_resid;
	return (error);
}

void
zfs_uioskip(zfs_uio_t *uio, size_t n)
{
	/*
	 * Skip n bytes in the uio without copying any data.  Advances
	 * uio_offset, uio_resid, AND the iov pointer/length for each
	 * iovec the skipped range crosses.  Failing to advance the iov
	 * causes subsequent zfs_uiomove() / zfs_uio_fault_move() calls
	 * to copy from the wrong source byte (the address held in
	 * iov[0].iov_base before the skip), which manifests as the
	 * second ZFS record of a single multi-record write being
	 * populated from the first record's source data.
	 */
	if (n > zfs_uio_resid(uio))
		return;

	struct uio *suio = GET_UIO_STRUCT(uio);
	size_t remaining = n;
	while (remaining > 0 && suio->uio_resid > 0) {
		struct iovec *iov = suio->uio_iov;
		if (iov->iov_len == 0) {
			suio->uio_iov++;
			suio->uio_iovcnt--;
			continue;
		}
		size_t cnt = iov->iov_len;
		if (cnt > remaining)
			cnt = remaining;
		iov->iov_base = (char *)iov->iov_base + cnt;
		iov->iov_len -= cnt;
		suio->uio_offset += cnt;
		suio->uio_resid -= cnt;
		remaining -= cnt;
	}
}

int
zfs_uio_fault_move(void *p, size_t n, zfs_uio_rw_t dir, zfs_uio_t *uio)
{
	ASSERT3U(zfs_uio_rw(uio), ==, dir);
	return (uiomove(p, n, GET_UIO_STRUCT(uio)));
}

boolean_t
zfs_uio_page_aligned(zfs_uio_t *uio)
{
	const struct iovec *iov = GET_UIO_STRUCT(uio)->uio_iov;

	for (int i = zfs_uio_iovcnt(uio); i > 0; iov++, i--) {
		uintptr_t addr = (uintptr_t)iov->iov_base;
		size_t size = iov->iov_len;
		if ((addr & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1)))
			return (B_FALSE);
	}
	return (B_TRUE);
}

/*
 * Free the pages array allocated by zfs_uio_get_dio_pages_alloc().
 *
 * On OSv we only allocated the pointer array itself (the pages are
 * caller-owned IOV buffers); simply free the array.
 */
void
zfs_uio_free_dio_pages(zfs_uio_t *uio, zfs_uio_rw_t rw)
{
	(void) rw;

	if (uio->uio_dio.pages != NULL) {
		vmem_free(uio->uio_dio.pages,
		    uio->uio_dio.npages * sizeof (void *));
		uio->uio_dio.pages = NULL;
		uio->uio_dio.npages = 0;
	}
	uio->uio_extflg &= ~UIO_DIRECT;
}

/*
 * Populate uio->uio_dio.pages from the IOV array for Direct I/O.
 *
 * On OSv (single address space unikernel), "pages" are simply PAGE_SIZE-
 * aligned virtual-address pointers into the caller's buffer - no pinning
 * is needed.  We require that iov_base and iov_len are both PAGE_SIZE-
 * aligned (enforced upstream by zfs_uio_page_aligned()).
 *
 * After this call:
 *   uio->uio_extflg  has UIO_DIRECT set
 *   uio->uio_dio.pages[i]  is the start of the i-th page across all iovecs
 *   uio->uio_dio.npages    is the total page count
 */
int
zfs_uio_get_dio_pages_alloc(zfs_uio_t *uio, zfs_uio_rw_t rw)
{
	(void) rw;
	struct uio *suio = GET_UIO_STRUCT(uio);
	const struct iovec *iov = suio->uio_iov;
	int iovcnt = suio->uio_iovcnt;

	/* Count total pages across all iovecs. */
	int npages = 0;
	for (int i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len == 0)
			continue;
		/* zfs_uio_page_aligned() guarantees these hold */
		ASSERT0((uintptr_t)iov[i].iov_base & PAGE_MASK);
		ASSERT0(iov[i].iov_len & PAGE_MASK);
		npages += (int)(iov[i].iov_len >> PAGE_SHIFT);
	}

	if (npages == 0)
		return (SET_ERROR(EINVAL));

	uio->uio_dio.pages = vmem_alloc(npages * sizeof (void *), KM_SLEEP);
	if (uio->uio_dio.pages == NULL)
		return (SET_ERROR(ENOMEM));

	/*
	 * Fill pages[]: each entry is the base virtual address of a
	 * PAGE_SIZE region in the caller's buffer.
	 */
	int page_idx = 0;
	for (int i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len == 0)
			continue;
		uintptr_t base = (uintptr_t)iov[i].iov_base;
		int n = (int)(iov[i].iov_len >> PAGE_SHIFT);
		for (int j = 0; j < n; j++) {
			uio->uio_dio.pages[page_idx++] =
			    (void *)(base + ((uintptr_t)j << PAGE_SHIFT));
		}
	}

	uio->uio_dio.npages = npages;
	uio->uio_extflg |= UIO_DIRECT;

	return (0);
}
