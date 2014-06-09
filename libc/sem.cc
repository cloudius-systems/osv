/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <semaphore.h>
#include <osv/semaphore.hh>
#include <osv/sched.hh>
#include <memory>
#include "libc.hh"

// FIXME: smp safety

struct indirect_semaphore : std::unique_ptr<semaphore> {
    explicit indirect_semaphore(unsigned units)
        : std::unique_ptr<semaphore>(new semaphore(units)) {}
};

indirect_semaphore& from_libc(sem_t* p)
{
    return *reinterpret_cast<indirect_semaphore*>(p);
}

int sem_init(sem_t* s, int pshared, unsigned val)
{
    static_assert(sizeof(indirect_semaphore) <= sizeof(*s), "sem_t overflow");
    new (s) indirect_semaphore(val);
    return 0;
}

int sem_destroy(sem_t *s)
{
    from_libc(s).~indirect_semaphore();
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

int sem_timedwait(sem_t* s, const struct timespec *abs_timeout)
{
    if ((abs_timeout->tv_sec < 0) || (abs_timeout->tv_nsec < 0) || (abs_timeout->tv_nsec > 1000000000LL)) {
        return libc_error(EINVAL);
    }

    sched::timer tmr(*sched::thread::current());
    osv::clock::wall::time_point time(std::chrono::seconds(abs_timeout->tv_sec) +
                                      std::chrono::nanoseconds(abs_timeout->tv_nsec));
    tmr.set(time);
    if (!from_libc(s)->wait(1, &tmr)) {
        return libc_error(ETIMEDOUT);
    }
    return 0;
}

int sem_trywait(sem_t* s)
{
    if (!from_libc(s)->trywait())
        return libc_error(EAGAIN);
    return 0;
}
