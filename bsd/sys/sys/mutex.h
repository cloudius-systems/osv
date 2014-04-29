#ifndef _BSD_MUTEX_H
#define _BSD_MUTEX_H
#include <machine/atomic.h>
#include <bsd/porting/synch.h>

#define MTX_SPIN	0x00000001      /* Spin lock (disables interrupts) */

#define mtx_lock_spin(x) do { mtx_lock(x); } while (0)
#define mtx_unlock_spin(x) do { mtx_unlock(x); } while (0)
#define msleep_spin_sbt(chan, mtx, wmesg, sbt, pr, flags) \
	do { msleep(chan, mtx, 0, wmesg, sbt); } while (0)

#endif
