#ifndef SYNC_STUB_H
#define SYNC_STUB_H

struct mtx {
    int __unused;
};

#define MTX_DEF     0x00000000  /* DEFAULT (sleep) lock */

#define MA_OWNED    9


void mtx_init(struct mtx *m, const char *name, const char *type, int opts);

void mtx_destroy(struct mtx *m);

void mtx_lock(struct mtx *mp);

void mtx_unlock(struct mtx *mp);

void mtx_assert(struct mtx *mp, int flag);

#define mtx_sleep(chan, mtx, pri, wmesg, timo) (void)0;

void critical_enter(void);

void critical_exit(void);

void cpu_spinwait(void);


#endif /* SYNC_STUB_H */
