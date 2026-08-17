// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv ABD (ARC buffer data) OS-specific structures.
 *
 * OSv does not have FreeBSD's vm_page_t, so we use a simple
 * chunk-based scatter implementation.
 */
#ifndef _ABD_OS_H
#define	_ABD_OS_H

#ifdef __cplusplus
extern "C" {
#endif

struct abd;

struct abd_scatter {
	uint_t		abd_offset;
	void		*abd_chunks[1]; /* actually variable-length */
};

struct abd_linear {
	void		*abd_buf;
};

/*
 * vm_page_t stub for Direct I/O support.
 * OSv doesn't have traditional VM pages; Direct I/O is not yet supported.
 * We provide the type and function declaration so dmu_direct.c compiles.
 */
typedef void *vm_page_t;

__attribute__((malloc))
struct abd *abd_alloc_from_pages(vm_page_t *, unsigned long, uint64_t);

#ifdef __cplusplus
}
#endif

#endif /* _ABD_OS_H */
