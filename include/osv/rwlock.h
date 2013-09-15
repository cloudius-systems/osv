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

#define RWLOCK_INITIALIZER {}

typedef struct rwlock {

#ifdef __cplusplus

public:
    rwlock();
    ~rwlock();

    // Reader
    void rlock();
    bool try_rlock();
    void runlock();
    bool try_upgrade();

    // Writer
    void wlock();
    bool try_wlock();
    void wunlock();
    void downgrade();
    bool wowned();

private:

    void writer_wait_lockable();
    void reader_wait_lockable();

    bool read_lockable();
    bool write_lockable();

#endif // __cplusplus

    mutex_t _mtx;
    condvar_t _cond_readers;
    condvar_t _cond_writers;

    unsigned _readers;
    unsigned _read_waiters;
    unsigned _write_waiters;

    void* _wowner;
    unsigned _wrecurse;

} rwlock_t;

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
