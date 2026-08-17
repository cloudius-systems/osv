// SPDX-License-Identifier: CDDL-1.0
#ifndef _SPL_OSV_PROCFS_LIST_H
#define	_SPL_OSV_PROCFS_LIST_H

#include <sys/kstat.h>
#include <sys/mutex.h>
#include <sys/list.h>

struct seq_file;

typedef struct procfs_list procfs_list_t;
struct procfs_list {
	void		*pl_private;
	kmutex_t	pl_lock;
	list_t		pl_list;
	uint64_t	pl_next_id;
	size_t		pl_node_offset;
	int		(*pl_show)(struct seq_file *f, void *p);
	int		(*pl_show_header)(struct seq_file *f);
	int		(*pl_clear)(procfs_list_t *procfs_list);
};

typedef struct procfs_list_node {
	list_node_t	pln_link;
	uint64_t	pln_id;
} procfs_list_node_t;

/* seq_file printf stub */
static inline void
seq_printf(struct seq_file *f, const char *fmt, ...)
{
	(void) f; (void) fmt;
}

/*
 * OSv procfs_list implementation.
 *
 * OSv has no /proc filesystem so we skip the kstat/procfs frontend, but
 * we MUST properly initialise pl_list and pl_lock and actually insert
 * elements into the list.  spa_txg_history_truncate() iterates the list
 * and calls ASSERT3P(entry, !=, NULL); if the list is left uninitialised
 * or empty while shl->size > 0 the assertion fires and the VM crashes
 * after ~100 TXG commits.
 */

#define	NODE_ID(procfs_list, obj) \
	(((procfs_list_node_t *)(((char *)(obj)) + \
	(procfs_list)->pl_node_offset))->pln_id)

static inline void
procfs_list_install(const char *module __attribute__((unused)),
    const char *submodule __attribute__((unused)),
    const char *name __attribute__((unused)),
    mode_t mode __attribute__((unused)),
    procfs_list_t *procfs_list,
    int (*show)(struct seq_file *f, void *p),
    int (*show_header)(struct seq_file *f),
    int (*clear)(procfs_list_t *procfs_list),
    size_t procfs_list_node_off)
{
	mutex_init(&procfs_list->pl_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&procfs_list->pl_list,
	    procfs_list_node_off + sizeof (procfs_list_node_t),
	    procfs_list_node_off + offsetof(procfs_list_node_t, pln_link));
	procfs_list->pl_show = show;
	procfs_list->pl_show_header = show_header;
	procfs_list->pl_clear = clear;
	procfs_list->pl_next_id = 1;
	procfs_list->pl_node_offset = procfs_list_node_off;
}

static inline void
procfs_list_uninstall(procfs_list_t *procfs_list __attribute__((unused)))
{
}

static inline void
procfs_list_destroy(procfs_list_t *procfs_list)
{
	list_destroy(&procfs_list->pl_list);
	mutex_destroy(&procfs_list->pl_lock);
}

static inline void
procfs_list_add(procfs_list_t *procfs_list, void *p)
{
	NODE_ID(procfs_list, p) = procfs_list->pl_next_id++;
	list_insert_tail(&procfs_list->pl_list, p);
}

#endif /* _SPL_OSV_PROCFS_LIST_H */
