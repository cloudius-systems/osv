// SPDX-License-Identifier: CDDL-1.0
/*
 * Identical to FreeBSD's list_impl.h - platform-independent linked list.
 */
#ifndef	_SYS_LIST_IMPL_H
#define	_SYS_LIST_IMPL_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct list_node {
	struct list_node *list_next;
	struct list_node *list_prev;
};

struct list {
	size_t	list_offset;
	struct list_node list_head;
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_LIST_IMPL_H */
