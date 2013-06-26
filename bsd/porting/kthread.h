#ifndef _OSV_BSD_KTHREAD_H
#define _OSV_BSD_KTHREAD_H

#include <sys/cdefs.h>

// Compat only
#define RFHIGHPID   (1<<18) /* use a pid higher than 10 (idleproc) */

__BEGIN_DECLS

struct proc {
    pid_t p_pid;
};

int
kproc_create(void (*func)(void *), void *arg,
                    struct proc **newpp, int flags, int pages, const char *fmt, ...);

struct proc get_curproc(void);
__END_DECLS

#define curproc ({ struct proc _p = get_curproc(); &_p; })
#endif
