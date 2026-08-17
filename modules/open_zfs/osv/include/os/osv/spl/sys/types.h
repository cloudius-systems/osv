// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL types - wraps compat types.h and adds OpenZFS-specific types.
 */
#ifndef _SPL_OSV_TYPES_H
#define	_SPL_OSV_TYPES_H

/*
 * Block the compat types.h and provide the types ourselves.
 * The compat types.h uses `typedef bool boolean_t` which maps to
 * `_Bool` in C23/GCC 15, causing va_arg promotion errors in fm.c.
 * We need boolean_t to be an int-compatible type.
 */
#define	_OPENSOLARIS_SYS_TYPES_H_

/* Get POSIX base types */
#include <sys/types.h>

/* Integer types matching compat types.h but with boolean_t as enum */
typedef unsigned char		uchar_t;
typedef unsigned short		ushort_t;
typedef unsigned int		uint_t;
typedef unsigned long		ulong_t;
typedef long long		longlong_t;
typedef unsigned long long	u_longlong_t;

typedef id_t		taskid_t;
typedef id_t		projid_t;
typedef id_t		poolid_t;
typedef id_t		zoneid_t;
typedef id_t		ctid_t;
typedef mode_t		o_mode_t;
typedef uint64_t	pgcnt_t;
typedef u_int		minor_t;

/*
 * boolean_t as enum, not _Bool, so it passes through va_arg correctly.
 */
#define	B_FALSE	0
#define	B_TRUE	1
typedef enum { _B_FALSE = 0, _B_TRUE = 1 }	boolean_t;

#ifdef _KERNEL
typedef short		index_t;
typedef off_t		offset_t;
typedef long		ptrdiff_t;
typedef int		major_t;
#else
typedef longlong_t	offset_t;
typedef u_longlong_t	u_offset_t;
typedef uint64_t	upad64_t;
typedef short		pri_t;
typedef int32_t		daddr32_t;
typedef int32_t		time32_t;
#ifndef _DISKADDR_T_DECLARED
typedef u_longlong_t	diskaddr_t;
#define	_DISKADDR_T_DECLARED
#endif
#endif

/*
 * Additional types needed by OpenZFS 2.3.6 that aren't in the compat types.h.
 */

/* loff_t - Linux file offset type */
#ifndef _LOFF_T_DECLARED
#define	_LOFF_T_DECLARED
typedef off_t	loff_t;
#endif

/* inode_timespec_t - Linux inode timestamp type */
typedef struct timespec inode_timespec_t;

/* zfs_kernel_param_t - kernel parameter (no-op on OSv) */
typedef void zfs_kernel_param_t;

/* Additional integer types if not defined */
#ifndef _DISKADDR_T_DECLARED
typedef unsigned long long	diskaddr_t;
#define	_DISKADDR_T_DECLARED
#endif

/* zidmap_t - ID mapping namespace (void on FreeBSD/OSv, struct on Linux) */
typedef void		zidmap_t;

/* umode_t - Linux-specific mode type */
typedef mode_t		umode_t;

/* Linux-style branch prediction hints */
#ifndef likely
#define	likely(x)	__builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define	unlikely(x)	__builtin_expect(!!(x), 0)
#endif

/*
 * Linux hash list types (used by zvol_impl.h and others).
 */
struct hlist_node {
	struct hlist_node	*next;
	struct hlist_node	**pprev;
};

struct hlist_head {
	struct hlist_node	*first;
};

#define	INIT_HLIST_HEAD(h)	((h)->first = NULL)

#define	hlist_add_head(node, head)	do {			\
	struct hlist_node *_first = (head)->first;		\
	(node)->next = _first;					\
	if (_first)						\
		_first->pprev = &(node)->next;			\
	(head)->first = (node);					\
	(node)->pprev = &(head)->first;				\
} while (0)

#define	hlist_del(node)		do {				\
	struct hlist_node *_next = (node)->next;			\
	struct hlist_node **_pprev = (node)->pprev;		\
	*_pprev = _next;					\
	if (_next)						\
		_next->pprev = _pprev;				\
	(node)->next = NULL;					\
	(node)->pprev = NULL;					\
} while (0)

#define	hlist_for_each(pos, head) \
	for (pos = (head)->first; pos; pos = pos->next)

#define	hlist_entry(ptr, type, member) \
	((type *)((char *)(ptr) - __builtin_offsetof(type, member)))

#endif /* _SPL_OSV_TYPES_H */
