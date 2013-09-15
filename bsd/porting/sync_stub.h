/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SYNC_STUB_H
#define SYNC_STUB_H

#include <sys/cdefs.h>
#include <osv/mutex.h>
#include <osv/rwlock.h>

struct mtx {
    mutex_t _mutex;
};

struct sx {
    rwlock_t _rw;
};

#define MTX_DEF     0x00000000  /* DEFAULT (sleep) lock */
#define MTX_DUPOK   0x00000010   /* Don't check for duplicate acquires */

#define MA_OWNED        (0x01)
#define MA_NOTOWNED     (0x02)
#define MA_RECURSED     (0x04)
#define MA_NOTRECURSED  (0x08)

__BEGIN_DECLS
void mtx_init(struct mtx *m, const char *name, const char *type, int opts);
void mtx_destroy(struct mtx *m);
void mtx_lock(struct mtx *mp);
void mtx_unlock(struct mtx *mp);
void mtx_assert(struct mtx *mp, int flag);

void sx_init(struct sx *m, const char *name);
void sx_xlock(struct sx *mp);
void sx_xunlock(struct sx *mp);
void sx_slock(struct sx *mp);
void sx_sunlock(struct sx *mp);
#define sx_assert(...) do { } while (0)

__END_DECLS

#define mtx_sleep(chan, mtx, pri, wmesg, timo) msleep(chan, mtx, pri, wmesg, timo)
#define MTX_SYSINIT(a, b, c, d) 

#endif /* SYNC_STUB_H */
