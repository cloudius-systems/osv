#include <semaphore.h>
#include <osv/semaphore.hh>
#include "libc.hh"

// FIXME: smp safety

semaphore* from_libc(sem_t* p)
{
    return reinterpret_cast<semaphore*>(p);
}

int sem_init(sem_t* s, int pshared, unsigned val)
{
    static_assert(sizeof(semaphore) <= sizeof(*s), "sem_t overflow");
    new (s) semaphore(val);
    return 0;
}

int sem_post(sem_t* s)
{
    from_libc(s)->post();
    return 0;
}

int sem_wait(sem_t* s)
{
    from_libc(s)->wait();
    return 0;
}

int sem_trywait(sem_t* s)
{
    if (!from_libc(s)->trywait())
        return libc_error(EAGAIN);
    return 0;
}
