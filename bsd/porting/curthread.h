/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _BSD_PORTING_CURTHREAD_H
#define _BSD_PORTING_CURTHREAD_H

#include <bsd/porting/netport.h>
#include <bsd/porting/kthread.h>

__BEGIN_DECLS;

extern struct thread *get_curthread(void);
#define curthread	get_curthread()

__END_DECLS;

#endif /* _BSD_PORTING_CURTHREAD_H */
