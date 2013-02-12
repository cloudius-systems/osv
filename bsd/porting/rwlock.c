#include <stdint.h>
#include <porting/netport.h>
#include <porting/rwlock.h>

void rw_init_flags(struct rwlock *rw, const char *name, int opts)
{

}

void rw_destroy(struct rwlock *rw)
{

}

void rw_sysinit(void *arg)
{

}

void rw_sysinit_flags(void *arg)
{

}

int rw_wowned(struct rwlock *rw)
{
    return (0);
}

void _rw_wlock(struct rwlock *rw, const char *file, int line)
{

}

int _rw_try_wlock(struct rwlock *rw, const char *file, int line)
{
    return (0);
}

void _rw_wunlock(struct rwlock *rw, const char *file, int line)
{

}

void _rw_rlock(struct rwlock *rw, const char *file, int line)
{

}

int _rw_try_rlock(struct rwlock *rw, const char *file, int line)
{
    return (0);
}

void _rw_runlock(struct rwlock *rw, const char *file, int line)
{

}

void _rw_wlock_hard(struct rwlock *rw, uintptr_t tid, const char *file, int line)
{

}

void _rw_wunlock_hard(struct rwlock *rw, uintptr_t tid, const char *file, int line)
{

}

int _rw_try_upgrade(struct rwlock *rw, const char *file, int line)
{
    return (0);
}

void _rw_downgrade(struct rwlock *rw, const char *file, int line)
{

}
