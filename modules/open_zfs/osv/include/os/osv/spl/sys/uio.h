// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL UIO - OpenZFS uio abstraction layer
 *
 * This header provides the zfs_uio_t type and accessor macros.
 * The inline helper functions are in <sys/zfs_uio_os.h> which is
 * included later (after struct uio is complete).
 */
#ifndef _SPL_OSV_UIO_H
#define	_SPL_OSV_UIO_H

#include <sys/types.h>

/*
 * Include the real POSIX sys/uio.h for struct iovec.
 * Use #include_next to skip our own header and find the next one.
 */
#include_next <sys/uio.h>

/*
 * Forward declaration - struct uio is completed by osv/uio.h.
 */
struct uio;

/*
 * uio_extflg: extended flags
 */
#define	UIO_DIRECT	0x0001

typedef struct iovec	iovec_t;

/*
 * UIO segment type. OSv is a unikernel - always kernel space.
 */
typedef enum zfs_uio_seg {
	ZFS_UIO_SYSSPACE = 0,
	ZFS_UIO_USERSPACE = 1
} zfs_uio_seg_t;

#ifndef UIO_USERSPACE
#define	UIO_USERSPACE	1
#endif

typedef int zfs_uio_rw_t;
#define	ZFS_UIO_READ	0
#define	ZFS_UIO_WRITE	1

/*
 * Direct I/O structure (stubbed on OSv).
 * vm_page_t is defined as void* in abd_os.h; we use void** so
 * pages[index] is valid C (yields void*).
 */
typedef struct {
	void	**pages;
	int	npages;
} zfs_uio_dio_t;

typedef struct zfs_uio {
	struct uio	*uio;
	offset_t	uio_soffset;
	uint16_t	uio_extflg;
	zfs_uio_dio_t	uio_dio;
} zfs_uio_t;

/*
 * Accessor macros.
 * struct uio must be complete when these are actually expanded.
 */
#define	GET_UIO_STRUCT(u)	(u)->uio
#define	zfs_uio_segflg(u)	ZFS_UIO_SYSSPACE
#define	zfs_uio_offset(u)	GET_UIO_STRUCT(u)->uio_offset
#define	zfs_uio_resid(u)	GET_UIO_STRUCT(u)->uio_resid
#define	zfs_uio_iovcnt(u)	GET_UIO_STRUCT(u)->uio_iovcnt
#define	zfs_uio_iovlen(u, idx)	GET_UIO_STRUCT(u)->uio_iov[(idx)].iov_len
#define	zfs_uio_iovbase(u, idx)	GET_UIO_STRUCT(u)->uio_iov[(idx)].iov_base
#define	zfs_uio_td(u)		(NULL)
#define	zfs_uio_rw(u)		GET_UIO_STRUCT(u)->uio_rw
#define	zfs_uio_soffset(u)	(u)->uio_soffset
#define	zfs_uio_fault_disable(u, set)
#define	zfs_uio_prefaultpages(size, u)	(0)

/*
 * zfs_uio_setoffset, zfs_uio_setsoffset, zfs_uio_advance, zfs_uio_init
 * are defined as static inline in zfs_context_os.h (force-included),
 * after struct uio is complete.
 */
extern int zfs_uio_fault_move(void *p, size_t n, zfs_uio_rw_t dir,
    zfs_uio_t *uio);

#endif /* _SPL_OSV_UIO_H */
