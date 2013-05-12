#ifndef _BSD_PORTING_CURTHREAD_H
#define _BSD_PORTING_CURTHREAD_H

#include <bsd/porting/netport.h>

__BEGIN_DECLS;

extern struct thread *get_curthread(void);
#define curthread	get_curthread()

__END_DECLS;

#endif /* _BSD_PORTING_CURTHREAD_H */
