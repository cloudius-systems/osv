/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _PORTING_SYNCH_H_
#define _PORTING_SYNCH_H_

#include <bsd/porting/sync_stub.h>

/* See the FreeBSD sleep(9) manual entry for usage */

__BEGIN_DECLS
int msleep(void *chan, struct mtx *mtx, int priority, const char *wmesg,
     int timo);

int tsleep(void *chan, int priority, const char *wmesg, int timo);

void bsd_pause(const char *wmesg, int timo);

void wakeup(void* chan);

void wakeup_one(void* chan);
__END_DECLS

#endif /* _PORTING_SYNCH_H_ */
