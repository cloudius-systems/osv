#include <porting/sync_stub.h>

void mtx_init(struct mtx *m, const char *name, const char *type, int opts)
{

}

void mtx_destroy(struct mtx *m)
{

}

void mtx_lock(struct mtx *mp)
{

}

void mtx_unlock(struct mtx *mp)
{
}

void mtx_assert(struct mtx *mp, int flag)
{

}

void critical_enter(void)
{

}

void critical_exit(void)
{

}

void cpu_spinwait(void)
{

}
