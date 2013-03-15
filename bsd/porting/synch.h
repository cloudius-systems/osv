#ifndef _PORTING_SYNCH_H_
#define _PORTING_SYNCH_H_

#include <bsd/porting/sync_stub.h>

/* See the FreeBSD sleep(9) manual entry for usage */

int msleep(void *chan, struct mtx *mtx, int priority, const char *wmesg,
     int timo);

void wakeup(void* chan);

void wakeup_one(void* chan);

#endif /* _PORTING_SYNCH_H_ */
