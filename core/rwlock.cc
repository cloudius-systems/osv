/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <mutex>
#include "sched.hh"
#include <osv/rwlock.h>

rwlock::rwlock()
    : _readers(0),
      _read_waiters(0),
      _write_waiters(0),
      _wowner(nullptr),
      _wrecurse(0)
{ }

rwlock::~rwlock()
{
    assert(_wowner == nullptr);
    assert(_readers == 0);
    assert(_read_waiters == 0);
    assert(_write_waiters == 0);
}

void rwlock::rlock()
{
    std::lock_guard<mutex> guard(_mtx);
    reader_wait_lockable();

    _readers++;
}

bool rwlock::try_rlock()
{
    std::lock_guard<mutex> guard(_mtx);
    if (!read_lockable()) {
        return false;
    }

    _readers++;
    return true;
}

void rwlock::runlock()
{
    bool need_wake = false;

    WITH_LOCK(_mtx) {
        assert(_wowner == nullptr);
        assert(_readers > 0);

        // If we are the last reader and we have a write waiter,
        // then wake up one writer
        if ((--_readers == 0) && (_write_waiters)) {
            need_wake = true;
        }
    }

    // wake() only after releasing the mutex
    if (need_wake) {
        _cond_writers.wake_one();
    }
}

bool rwlock::try_upgrade()
{
    std::lock_guard<mutex> guard(_mtx);

    // if we don't have any write waiters and we are the only reader
    if ((_readers == 1) && (!_write_waiters)) {
        assert(_wowner == nullptr);
        _readers = 0;
        _wowner = sched::thread::current();
        return true;
    }

    return false;
}

void rwlock::wlock()
{
    std::lock_guard<mutex> guard(_mtx);
    writer_wait_lockable();

    // recursive write lock
    if (_wowner == sched::thread::current()) {
        _wrecurse++;
    }

    _wowner = sched::thread::current();
}

bool rwlock::try_wlock()
{
    std::lock_guard<mutex> guard(_mtx);
    if (!write_lockable()) {
        return false;
    }

    // recursive write lock
    if (_wowner == sched::thread::current()) {
        _wrecurse++;
    }

    _wowner = sched::thread::current();
    return true;
}

void rwlock::wunlock()
{
    WITH_LOCK(_mtx) {
        assert(_wowner == sched::thread::current());

        if (_wrecurse > 0) {
            _wrecurse--;
        } else {
            _wowner = nullptr;
        }
    }

    // wake() only after releasing the mutex
    if (_write_waiters) {
        _cond_writers.wake_one();
    } else if (_read_waiters) {
        _cond_readers.wake_all();
    }
}

void rwlock::downgrade()
{
    WITH_LOCK(_mtx) {
        assert(_wowner == sched::thread::current());

        // I'm aware this implementation is ugly but it does the trick for the
        // time being.
        while (_wrecurse) this->wunlock();
        this->wunlock();
    }

    // FIXME: Writers that already wait get precedence, so this function can
    // block, there's only one user in sys/netinet/if_ether.c
    // and we need to make sure that it's ok to block here.

    this->rlock();
}

bool rwlock::wowned()
{
    return (sched::thread::current() == _wowner);
}

bool rwlock::read_lockable()
{
    return ((!_wowner) && (!_write_waiters));
}

bool rwlock::write_lockable()
{
    return ((_wowner == sched::thread::current()) ||
            ((!_readers) && (!_wowner)));
}

void rwlock::writer_wait_lockable()
{
    while (true) {
        if (write_lockable()) {
            return;
        }

        _write_waiters++;
        _cond_writers.wait(&_mtx, nullptr);
        _write_waiters--;
    }
}

void rwlock::reader_wait_lockable()
{
    while (true) {
        if (read_lockable()) {
            return;
        }

        _read_waiters++;
        _cond_readers.wait(&_mtx, nullptr);
        _read_waiters--;
    }
}

void rwlock_init(rwlock_t* rw)
{
    new (rw) rwlock;
}

void rwlock_destroy(rwlock_t* rw)
{
    rw->~rwlock();
}

void rw_rlock(rwlock_t* rw)
{
    rw->rlock();
}

void rw_wlock(rwlock_t* rw)
{
    rw->wlock();
}

int rw_try_rlock(rwlock_t* rw)
{
    return rw->try_rlock();
}

int rw_try_wlock(rwlock_t* rw)
{
    return rw->try_wlock();
}

void rw_runlock(rwlock_t* rw)
{
    rw->runlock();
}

void rw_wunlock(rwlock_t* rw)
{
    rw->wunlock();
}

int rw_try_upgrade(rwlock_t* rw)
{
    return rw->try_upgrade();
}

void rw_downgrade(rwlock_t* rw)
{
    rw->downgrade();
}
