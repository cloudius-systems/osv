// SPDX-License-Identifier: CDDL-1.0
/*
 * OpenZFS doubly-linked list. Identical to FreeBSD's SPL list.h.
 */
#ifndef	_SYS_LIST_H
#define	_SYS_LIST_H

#include <sys/list_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct list_node list_node_t;
typedef struct list list_t;

void list_create(list_t *, size_t, size_t);
void list_destroy(list_t *);

void list_insert_after(list_t *, void *, void *);
void list_insert_before(list_t *, void *, void *);
void list_insert_head(list_t *, void *);
void list_insert_tail(list_t *, void *);
void list_remove(list_t *, void *);
void *list_remove_head(list_t *);
void *list_remove_tail(list_t *);
void list_move_tail(list_t *, list_t *);

void *list_head(list_t *);
void *list_tail(list_t *);
void *list_next(list_t *, void *);
void *list_prev(list_t *, void *);
int list_is_empty(list_t *);

void list_link_init(list_node_t *);
void list_link_replace(list_node_t *, list_node_t *);

int list_link_active(list_node_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_LIST_H */
