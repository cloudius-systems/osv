#include <assert.h>
#include <stdint.h>
#include <osv/mutex.h>
#include <bsd/porting/netport.h>
#include <bsd/porting/rwlock.h>

void rw_init_flags(struct rwlock *rw, const char *name, int opts)
{
    mutex_init(&rw->_mutex);
}

void rw_destroy(struct rwlock *rw)
{
    mutex_destroy(&rw->_mutex);
}

int rw_wowned(struct rwlock *rw)
{
    /* Is owned by current thread */
    return (mutex_owned(&rw->_mutex));
}

void _rw_wlock(struct rwlock *rw, const char *file, int line)
{
    _rw_rlock(rw, file, line);
}

int _rw_try_wlock(struct rwlock *rw, const char *file, int line)
{
    return(_rw_try_rlock(rw, file, line));
}

void _rw_wunlock(struct rwlock *rw, const char *file, int line)
{
    _rw_runlock(rw, file, line);
}

void _rw_rlock(struct rwlock *rw, const char *file, int line)
{
    mutex_lock(&rw->_mutex);
}

int _rw_try_rlock(struct rwlock *rw, const char *file, int line)
{
    return mutex_trylock(&rw->_mutex);
}

void _rw_runlock(struct rwlock *rw, const char *file, int line)
{
    mutex_unlock(&rw->_mutex);
}

int _rw_try_upgrade(struct rwlock *rw, const char *file, int line)
{
    /* The lock is already exclusive... */
    return (1);
}

void _rw_downgrade(struct rwlock *rw, const char *file, int line)
{
    /* No-op */
}
