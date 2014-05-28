/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __OSV_RWLOCK_H__
#define __OSV_RWLOCK_H__

#include <osv/mutex.h>
#include <osv/condvar.h>
#include <sys/cdefs.h>
#include <osv/waitqueue.hh>

#define RWLOCK_INITIALIZER {}

#ifdef __cplusplus

class rwlock;

// an rwlock pretending it is an ordinary lock for
// WITH_LOCK() and friends.  Taking the lock acquires
// it for reading.
//
// example: WITH_LOCK(my_rwlock.for_read()) { ... }
class rwlock_for_read {
private:
    rwlock_for_read() = default;
public:
    void lock();
    void unlock();
    friend class rwlock;
};

// an rwlock pretending it is an ordinary lock for
// WITH_LOCK() and friends.  Taking the lock acquires
// it for writing.
//
// example: WITH_LOCK(my_rwlock.for_write()) { ... }
class rwlock_for_write {
private:
    rwlock_for_write() = default;
public:
    void lock();
    void unlock();
    friend class rwlock;
};

struct rwlock : private rwlock_for_read, rwlock_for_write {

#else

struct rwlock {

#endif

#ifdef __cplusplus

public:
    rwlock();
    ~rwlock();

    // Reader
    void rlock();
    bool try_rlock();
    void runlock();
    bool try_upgrade();
    rwlock_for_read& for_read() { return *this; }

    // Writer
    void wlock();
    bool try_wlock();
    void wunlock();
    void downgrade();
    bool wowned();
    rwlock_for_write& for_write() { return *this; }

private:

    void writer_wait_lockable();
    void reader_wait_lockable();

    bool read_lockable();
    bool write_lockable();

    friend class rwlock_for_read;
    friend class rwlock_for_write;

#endif // __cplusplus

    mutex_t _mtx;
    unsigned _readers;
    waitqueue _read_waiters;
    waitqueue _write_waiters;

    void* _wowner;
    unsigned _wrecurse;

};

typedef struct rwlock rwlock_t;

#ifdef __cplusplus

inline void rwlock_for_read::lock()
{
    static_cast<rwlock*>(this)->rlock();
}

inline void rwlock_for_read::unlock()
{
    static_cast<rwlock*>(this)->runlock();
}

inline void rwlock_for_write::lock()
{
    static_cast<rwlock*>(this)->wlock();
}

inline void rwlock_for_write::unlock()
{
    static_cast<rwlock*>(this)->wunlock();
}

#endif

__BEGIN_DECLS
void rwlock_init(rwlock_t* rw);
void rwlock_destroy(rwlock_t* rw);
void rw_rlock(rwlock_t* rw);
void rw_wlock(rwlock_t* rw);
int rw_try_rlock(rwlock_t* rw);
int rw_try_wlock(rwlock_t* rw);
void rw_runlock(rwlock_t* rw);
void rw_wunlock(rwlock_t* rw);
int rw_try_upgrade(rwlock_t* rw);
void rw_downgrade(rwlock_t* rw);
__END_DECLS

#endif // !__RWLOCK_H__
