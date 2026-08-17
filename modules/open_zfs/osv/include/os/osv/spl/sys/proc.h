// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL proc - standalone (avoids compat proc.h that uses PVM/PRIBIO)
 */
#ifndef _SPL_OSV_PROC_H
#define	_SPL_OSV_PROC_H

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Priority definitions.
 * The compat proc.h uses PVM and PRIBIO from FreeBSD priority.h,
 * which are not available on OSv. Use numeric values directly.
 * PVM = PRI_MIN_KERN + 4 = 64 + 4 = 68
 * PRIBIO = PRI_MIN_KERN + 12 = 64 + 12 = 76
 */
#define	minclsyspri	76
#define	maxclsyspri	68
#define	defclsyspri	72

/* Max CPUs - match netport.h */
#ifndef MAXCPU
#define	MAXCPU		(sizeof (unsigned long) * 8)
#endif
#define	max_ncpus	MAXCPU
#define	boot_max_ncpus	MAXCPU

#define	TS_RUN	0

#define	p0	proc0

#define	t_tid	td_tid

typedef	short		pri_t;

/*
 * Thread types - OSv uses struct thread from netport.h as an opaque type.
 * We forward-declare it here.
 */
struct thread;
struct proc;

typedef	struct thread	_kthread;
typedef	struct thread	kthread_t;
typedef struct thread	*kthread_id_t;
typedef struct proc	proc_t;

extern struct proc *zfsproc;
extern struct proc proc0;

/*
 * Thread/process functions.
 * On OSv, kthread_add is provided by the compat layer.
 */
extern int kthread_add(void (*)(void *), void *, struct proc *,
    struct thread **, int, int, const char *, ...);

static __inline kthread_t *
thread_create(caddr_t stk, size_t stksize, void (*proc)(void *), void *arg,
    size_t len, proc_t *pp, int state, pri_t pri)
{
	kthread_t *td = NULL;
	int error;

	(void) stk;
	(void) stksize;
	(void) len;
	(void) pp;
	(void) state;
	(void) pri;

	error = kthread_add(proc, arg, NULL, &td, 0, 0, "solthread-%p", proc);
	(void) error;
	return (td);
}

extern void kthread_exit(void) __attribute__((noreturn));
#define	thread_exit()	kthread_exit()

/*
 * curthread - current thread pointer.
 * Implemented as a macro wrapping get_curthread() (exported from
 * loader.elf via bsd/porting/kthread.cc).  This avoids needing a
 * global variable whose address the dynamic linker must resolve.
 */
extern struct thread *get_curthread(void);
#define	curthread	((struct thread *)get_curthread())

/*
 * Process comparison - OSv is single-process, so always return true.
 * Note: curproc is defined as a macro in zfs_context_os.h
 */
static inline boolean_t
zfs_proc_is_caller(proc_t *p)
{
	(void) p;
	return (B_TRUE);
}

#ifdef __cplusplus
}
#endif

#endif /* _SPL_OSV_PROC_H */
