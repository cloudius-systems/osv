#include <osv/mutex.h>

#include <bsd/porting/sync_stub.h>

void mtx_init(struct mtx *m, const char *name, const char *type, int opts)
{
    mutex_init(&m->_mutex);
}

void mtx_destroy(struct mtx *m)
{
    mutex_destroy(&m->_mutex);
}

void mtx_lock(struct mtx *mp)
{
    mutex_lock(&mp->_mutex);
}

void mtx_unlock(struct mtx *mp)
{
    mutex_unlock(&mp->_mutex);
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
