// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL taskq - standalone (avoids compat chain through taskqueue.h)
 *
 * The compat taskq.h -> contrib taskq.h -> sys/taskqueue.h chain fails
 * because taskqueue.h uses __printflike and FreeBSD-specific constructs.
 * We provide the taskq types and functions directly.
 */
#ifndef _SPL_OSV_TASKQ_H
#define	_SPL_OSV_TASKQ_H

#include <sys/types.h>
#include <sys/proc.h>
/*
 * Pull in the real BSD "struct task" (32 bytes) rather than faking it.
 * taskq_ent_t embeds an ostask that taskq_dispatch_safe() initializes with
 * TASK_INIT() and hands to taskqueue_enqueue(); both are compiled against
 * this same layout in opensolaris_taskq.c.  A mismatched (smaller) struct
 * task here would let those writes overflow the embedded entry and corrupt
 * the enclosing zio_t/dbuf_t.  _task.h only drags in <sys/queue.h>, so it
 * avoids the __printflike chain that <sys/taskqueue.h> would impose.
 */
#include <sys/_task.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	TASKQ_NAMELEN	31

struct taskqueue;

struct taskq {
	struct taskqueue	*tq_queue;
	int			tq_nthreads;
};

typedef struct taskq taskq_t;
typedef uintptr_t taskqid_t;
typedef void (task_func_t)(void *);

/*
 * Public flags for taskq_create(): bit range 0-15
 */
#define	TASKQ_PREPOPULATE	0x0001
#define	TASKQ_CPR_SAFE		0x0002
#define	TASKQ_DYNAMIC		0x0004
#define	TASKQ_THREADS_CPU_PCT	0x0008
#define	TASKQ_DC_BATCH		0x0010

/*
 * Flags for taskq_dispatch.
 */
#define	TQ_SLEEP	0x00
#define	TQ_NOSLEEP	0x01
#define	TQ_NOQUEUE	0x02
#define	TQ_NOALLOC	0x04
#define	TQ_FRONT	0x08

#define	TASKQID_INVALID		((taskqid_t)0)

extern taskq_t *system_taskq;
/* Global dynamic task queue for long delay */
extern taskq_t *system_delay_taskq;

extern void	taskq_init(void);
extern void	taskq_mp_init(void);

extern taskq_t	*taskq_create(const char *, int, pri_t, int, int, uint_t);
extern taskq_t	*taskq_create_synced(const char *, int, pri_t, int, int,
    uint_t, kthread_t ***);
extern taskq_t	*taskq_create_instance(const char *, int, int, pri_t, int,
    int, uint_t);
extern taskq_t	*taskq_create_proc(const char *, int, pri_t, int, int,
    struct proc *, uint_t);
/*
 * taskq_create_sysdc is provided as a macro in zfs_context_os.h
 * that maps to taskq_create. Don't declare it as extern here.
 */
extern taskqid_t taskq_dispatch(taskq_t *, task_func_t, void *, uint_t);
extern taskqid_t taskq_dispatch_delay(taskq_t *, task_func_t, void *,
    uint_t, clock_t);
extern void	nulltask(void *);
extern void	taskq_destroy(taskq_t *);
extern void	taskq_wait(taskq_t *);
extern void	taskq_wait_id(taskq_t *, taskqid_t);
extern void	taskq_wait_outstanding(taskq_t *, taskqid_t);
extern void	taskq_suspend(taskq_t *);
extern int	taskq_suspended(taskq_t *);
extern void	taskq_resume(taskq_t *);
extern int	taskq_member(taskq_t *, kthread_t *);
extern int	taskq_cancel_id(taskq_t *, taskqid_t, boolean_t);
extern taskq_t	*taskq_of_curthread(void);

/*
 * ostask - the unit of work the OSv taskqueue glue actually enqueues.
 * Must match the definition in <opensolaris/sys/taskq.h> used by
 * opensolaris_taskq.c (taskq_dispatch_safe / taskq_run_safe).
 */
struct ostask {
	struct task	 ost_task;
	task_func_t	*ost_func;
	void		*ost_arg;
};

/*
 * taskq_ent_t - caller-owned task queue entry.
 *
 * The entry embeds its own ostask so taskq_dispatch_ent() can route through
 * taskq_dispatch_safe() (no auto-free; matches the FreeBSD spl behaviour where
 * the entry is owned by the caller).  The previous OSv shim heap-allocated a
 * throwaway ostask inside taskq_dispatch() and stashed its pointer in
 * tqent_id, which taskq_run() then freed -- leaving tqent_id dangling for the
 * next taskq_empty_ent()/taskq_wait_id() to dereference.  Embedding the ostask
 * removes that use-after-free entirely.
 */
typedef struct taskq_ent {
	struct ostask	 tqent_ostask;
	task_func_t	*tqent_func;
	void		*tqent_arg;
	taskqid_t	 tqent_id;
	uint_t		 tqent_type;
	volatile uint_t	 tqent_rc;
} taskq_ent_t;

extern void taskq_dispatch_ent(taskq_t *, task_func_t, void *, uint_t,
    taskq_ent_t *);
extern int taskq_empty_ent(taskq_ent_t *);
extern void taskq_init_ent(taskq_ent_t *);

/*
 * OSv compat extension: taskq_dispatch_safe with caller-provided ostask.
 */
taskqid_t taskq_dispatch_safe(taskq_t *tq, task_func_t func, void *arg,
    u_int flags, struct ostask *task);

#ifdef __cplusplus
}
#endif

#endif /* _SPL_OSV_TASKQ_H */
