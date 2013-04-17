#ifndef SYNC_STUB_H
#define SYNC_STUB_H

#include <osv/mutex.h>

struct mtx {
    mutex_t _mutex;
};

#define MTX_DEF     0x00000000  /* DEFAULT (sleep) lock */
#define MTX_DUPOK   0x00000010   /* Don't check for duplicate acquires */

#define MA_OWNED        (0x01)
#define MA_NOTOWNED     (0x02)
#define MA_RECURSED     (0x04)
#define MA_NOTRECURSED  (0x08)

void mtx_init(struct mtx *m, const char *name, const char *type, int opts);

void mtx_destroy(struct mtx *m);

void mtx_lock(struct mtx *mp);

void mtx_unlock(struct mtx *mp);

void mtx_assert(struct mtx *mp, int flag);

#define mtx_sleep(chan, mtx, pri, wmesg, timo) (void)0;


#endif /* SYNC_STUB_H */
