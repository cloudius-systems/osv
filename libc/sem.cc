/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <semaphore.h>
#include <fcntl.h>
#include <osv/semaphore.hh>
#include <osv/sched.hh>
#include <memory>
#include "libc.hh"
#include <unordered_map>

// FIXME: smp safety

struct posix_semaphore : semaphore {
    private:
        int references;
        bool named;
    public:
        posix_semaphore(int units, int refs, bool named)
        : semaphore(units), references(refs), named(named) {}

        void add_reference(){
            references++;
        }

        void remove_reference(){
            references--;
        }

        bool not_referenced(){
            return references <= 0;
        }

        void unlink(){
            named = false;
        }

        bool linked(){
            return named;
        }
};

using indirect_semaphore = std::unique_ptr<posix_semaphore>;

indirect_semaphore& from_libc(sem_t* p)
{
    return *reinterpret_cast<indirect_semaphore*>(p);
}

OSV_LIBC_API
int sem_init(sem_t* s, int pshared, unsigned val)
{
    static_assert(sizeof(indirect_semaphore) <= sizeof(*s), "sem_t overflow");
    posix_semaphore *sem = new posix_semaphore(val, 1, false); 
    new (s) indirect_semaphore(sem);
    return 0;
}

OSV_LIBC_API
int sem_destroy(sem_t *s)
{
    from_libc(s).~indirect_semaphore();
    return 0;
}

OSV_LIBC_API
int sem_post(sem_t* s)
{
    from_libc(s)->post();
    return 0;
}

OSV_LIBC_API
int sem_wait(sem_t* s)
{
    from_libc(s)->wait();
    return 0;
}

OSV_LIBC_API
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

OSV_LIBC_API
int sem_trywait(sem_t* s)
{
    if (!from_libc(s)->trywait())
        return libc_error(EAGAIN);
    return 0;
}

static std::unordered_map<std::string, indirect_semaphore> named_semaphores;
static mutex named_semaphores_mutex;

OSV_LIBC_API
sem_t *sem_open(const char *name, int oflag, mode_t mode, unsigned int value)
{
    SCOPE_LOCK(named_semaphores_mutex);
    auto iter = named_semaphores.find(std::string(name));
    
    if (iter != named_semaphores.end()) {
        //opening already named semaphore
        if (oflag & O_EXCL && oflag & O_CREAT) {
            errno = EEXIST;
            return SEM_FAILED;
        }

        iter->second->add_reference();
        return reinterpret_cast<sem_t*>(&(iter->second));
    }
    else if (oflag & O_CREAT) {
        //creating new semaphore
        if (value > SEM_VALUE_MAX) {
            errno = EINVAL;
            return SEM_FAILED;
        }
        
        named_semaphores.emplace(std::string(name),
            std::unique_ptr<posix_semaphore>(new posix_semaphore(value, 1, true)));
        return reinterpret_cast<sem_t *>(&named_semaphores[std::string(name)]);
    }
    
    errno = ENOENT;
    return SEM_FAILED;
}

OSV_LIBC_API
int sem_unlink(const char *name)
{
    SCOPE_LOCK(named_semaphores_mutex);
    auto iter = named_semaphores.find(std::string(name));
    if (iter != named_semaphores.end()) {
        iter->second->unlink();
        if (iter->second->not_referenced()) {
            sem_destroy(reinterpret_cast<sem_t *>(&iter->second));
        }
        named_semaphores.erase(iter);
        return 0;
    }
    
    errno = ENOENT;
    return -1;
}

OSV_LIBC_API
int sem_close(sem_t *sem)
{
    SCOPE_LOCK(named_semaphores_mutex);
    indirect_semaphore &named_sem = from_libc(sem);
    named_sem->remove_reference();
    if (!named_sem->linked() && named_sem->not_referenced()) {
        sem_destroy(sem);
    }
    return 0;
}
