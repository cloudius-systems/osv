/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 * Copyright (C) 2025 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <mutex>
#include <osv/sched.hh>
#include <osv/rwlock.h>
#include <osv/export.h>

using namespace sched;

//This read-write lock implementation is a 2nd version and aims to improve
//performance especially for read heavy workflows.
//
//It is based on similar ideas used in the read-write lock implementation in Golang
//and described in this article by Eli Bendersky - "A more efficient writer-preferring RW lock"
//in https://eli.thegreenplace.net/2019/implementing-reader-writer-locks/. However, it uses
//OSv primitives - wait()/wake() - to facilitate synchronization between writer and readers.
//Also, I believe, it uses a novel idea of using single field - `_readers` - to count
//both owning readers and pending readers at the same time (it could be that I re-invented
//what somebody else already invented before me). Last but not least, I was inspired
//by another implementation using atomics only and thread::yield() by Christian Dietrich
//(see https://gitlab.ibr.cs.tu-bs.de/kanoun/osv/-/blob/dbdb91c1c7d1a16c7ad2093c0f61fe5914656f2d/core/rwlock.cc)
//
//In essence, this implementation uses a mutex to synchronize access between writers
//and atomics to synchronize access between readers and writers.
//
//The fields:
// _wmtx - writers' mutex to enforce only single writer can acquire the lock
//
// _writer_wait - boolean field used to synchronize between last reader runlock()
//                and a pending writer in wlock()
//
// _readers -  multi-purpose 32-bit field manipulated atomically and used to:
//             - indicate if the lock is owned by a writer (31st bit, see LOCK_INDICATOR)
//             - indicate if there is a pending writer (30th bit, see WRITE_INDICATOR);
//               it is set by a pending writer in wlock() before entering a wait to block
//               any new readers from acquiring the lock
//             - count readers that own the lock (bits 29-16, see READER_MASKS); max of 16383
//               we actually add 0x10000 for each rlock()
//             - count pending readers (bits 15-0); max of 65535
//
// _read_waiters - lock-less multiple-producer single-consumer queue used to register
//                 pending readers; pending reader pushes its thread after all unsuccessful
//                 attempts to acquire a read lock in rlock(), the owning writer pops waiting
//                 readers of the queue and wakes them up before releasing the lock in
//                 wunlock() and downgrade()
//
//The atomic operations manipulating _readers and _writer_wait use the "acquire/release"
//memory ordering to make sure the changes are visible across the CPUs with weak-memory model
//(for example ARM). The "acquire/release" does not have any effect on x86_64. Please
//read https://davekilian.com/acquire-release.html, if you want to better understand
//the "acquire/release" memory order.
//
//This RW lock should be fair for both writers and readers. Even though pending writer sets
//WRITE_INDICATOR bit to block new readers, the owning writer in wunlock() wakes pending readers
//to acquire the lock for reading. Therefore, neither readers nor writers should starve.

static constexpr unsigned LOCK_INDICATOR       = 0x80000000;
static constexpr unsigned WRITE_INDICATOR      = 0x40000000;
static constexpr unsigned READER_MASKS         = 0x3fff0000;
static constexpr unsigned IND_READ_MASK        = 0xffff0000;
static constexpr unsigned WAITING_READERS_MASK = 0x0000ffff;
static constexpr unsigned READER_LOCK_INC      = 0x00010000;

rwlock::rwlock()
    : _readers(0)
{}

rwlock::~rwlock()
{
    assert(_readers == 0);
    assert(_read_waiters.empty());
}

bool rwlock::try_rlock()
{
    //If there is no owning or pending writer, try to acquire the lock for reading
    //by adding READER_LOCK_INC to the _readers atomically
    if (_readers < WRITE_INDICATOR) {
        std::atomic<unsigned> *readers = reinterpret_cast<std::atomic<unsigned>*>(&_readers);
        unsigned prev_readers = readers->load(std::memory_order_acquire);
        if (readers->compare_exchange_strong(prev_readers, prev_readers + READER_LOCK_INC, std::memory_order_acq_rel)) {
            return true;
        }
    }
    return false;
}

void rwlock::rlock()
{
    std::atomic<unsigned> *readers = reinterpret_cast<std::atomic<unsigned>*>(&_readers);

    //Try to acquire a lock for reading by adding READER_LOCK_INC to the _readers atomically
    //The loop will stop once a new writer enters wlock() past the _wmtx.lock()
    //and sets WRITE_INDICATOR, or writer has already locked it before, so
    //the WRITE_INDICATOR is already set.
    //It may race with wunlock() which removes WRITE_INDICATOR
    //The loop may also race with other threads calling rlock() or runlock()
    unsigned prev_readers = readers->load(std::memory_order_acquire);
    while (prev_readers < WRITE_INDICATOR) {
        if (readers->compare_exchange_weak(prev_readers, prev_readers + READER_LOCK_INC, std::memory_order_acq_rel)) {
            return;
        }
    }

    //We stopped because the WRITE_INDICATOR was set so let us add ourselves to
    //the pending readers
    //This may race with wunlock() which removes WRITE_INDICATOR
    //1) if we win (1st) - wunlock() will see the latest number of pending readers including us
    //2) if we lose (2nd) - we will see WRITE_INDICATOR off (the while condition true)
    prev_readers = readers->fetch_add(1, std::memory_order_acq_rel) + 1;
    //We add 1 above, because this is the value we expect to modify below
    //Let us try in a loop again because maybe the 2nd scenario above is true
    while (prev_readers < WRITE_INDICATOR) {
        if (readers->compare_exchange_weak(prev_readers, prev_readers + READER_LOCK_INC - 1, std::memory_order_acq_rel)) {
            return;
        }
    }

    //We have failed to acquire the lock for reading and bumped the pending readers count
    //Let us wait until wunlock() or downgrade() wakes us and bumps the _readers
    //by (READER_LOCK_INC - 1) on our behalf
    lockfree::linked_item<thread*> read_waiter(thread::current());
    _read_waiters.push(&read_waiter);
    std::atomic<thread*> *value = reinterpret_cast<std::atomic<thread*>*>(&read_waiter.value);
    thread::wait_until( [value] {
        return value->load(std::memory_order_acquire) == nullptr;
    });
}

void rwlock::runlock()
{
    //Release the lock for reading by subtracting READER_LOCK_INC atomically
    std::atomic<unsigned> *readers = reinterpret_cast<std::atomic<unsigned>*>(&_readers);

    unsigned prev_readers = readers->fetch_add(-READER_LOCK_INC, std::memory_order_acq_rel);

    assert(prev_readers > 0);
    assert(prev_readers < LOCK_INDICATOR);

    //Wake potential pending writer if any, if we are the last owning reader
    if ((prev_readers & READER_MASKS) == READER_LOCK_INC && (prev_readers & WRITE_INDICATOR) && (prev_readers & LOCK_INDICATOR) == 0) {
        //Wake the _wmtx owner - pending writer - if not null
        auto pending_writer = _wmtx.get_owner();
        if (pending_writer) {
            //Synchronize with pending writer by setting _writer_wait to false
            std::atomic<bool> *writer_wait = reinterpret_cast<std::atomic<bool>*>(&_writer_wait);
            writer_wait->store(false, std::memory_order_release);
            pending_writer->wake();
        }
    }
}

bool rwlock::try_upgrade()
{
    //First we need to acquire the writers' mutex
    if (!_wmtx.try_lock()) {
        return false;
    }

    std::atomic<unsigned> *readers = reinterpret_cast<std::atomic<unsigned>*>(&_readers);

    unsigned prev_readers = readers->load(std::memory_order_acquire);
    //We have to be the only owning reader and there are no other pending writers
    if ((prev_readers & IND_READ_MASK) == READER_LOCK_INC) { // LOCK_INDICATOR and WRITE_INDICATOR are off
        if (readers->compare_exchange_strong(prev_readers, WRITE_INDICATOR | LOCK_INDICATOR | (prev_readers & WAITING_READERS_MASK), std::memory_order_acq_rel)) {
            // we've won the race
            return true;
        }
    }

    //We either were not the only reader or have lost the race with a new reader or writer
    _wmtx.unlock();
    return false;
}

bool rwlock::try_wlock()
{
    //First we need to acquire the writers' mutex
    if (!_wmtx.try_lock()) {
        return false;
    }

    std::atomic<unsigned> *readers = reinterpret_cast<std::atomic<unsigned>*>(&_readers);
    unsigned prev_readers = readers->load(std::memory_order_acquire);
    if ((prev_readers & READER_MASKS) == 0 && (prev_readers & LOCK_INDICATOR) == 0) { //No owning readers and writer
        if (readers->compare_exchange_strong(prev_readers, WRITE_INDICATOR | LOCK_INDICATOR | (prev_readers & WAITING_READERS_MASK), std::memory_order_acq_rel)) {
            // we've won the race
            return true;
        }
    } else if (prev_readers & LOCK_INDICATOR && _wmtx.owned()) { //Lock is acuired for writing and it is us
        return true;
    }

    //We have lost the race with a new reader or writer
    _wmtx.unlock();
    return false;
}

void rwlock::wlock()
{
    //Lock the writer mutex which may go to sleep
    _wmtx.lock();

    //At this point we are still a pending writer and the 1st from all the pending ones if any
    _writer_wait = true;

    //Lets set the write indicator in order to phase out the current readers and block new ones
    //from acquiring the lock for reading
    //This may race with the runlock() of the last reader
    std::atomic<unsigned> *readers = reinterpret_cast<std::atomic<unsigned>*>(&_readers);
    unsigned prev_readers = readers->fetch_or(WRITE_INDICATOR, std::memory_order_acq_rel);
    //1) If we lost (the fetch above was 2nd), then the count of owning readers per
    //   prev_readers should be 0, and runlock() will not see WRITE_INDICATOR, and
    //   therefore the last reader will not try to wake us and change _writer_wait.
    //   The compare_exchange_weak() down below will acquire the lock for writing if
    //   successful.
    //2) If we won, then the count of owning readers per prev_readers will be equal to
    //   READER_LOCK_INC and runlock() will see WRITE_INDICATOR and should wake us
    //   and set _writer_wait to false.
    //   The while loop down below will not enter and proceed to wait_until()

    //Check recursive
    if (prev_readers & LOCK_INDICATOR && _wmtx.owned()) {
        return;
    }

    //Try to set LOCK_INDICATOR if no active readers and no LOCK_INDICATOR set already (is LOCK_INDICATOR necessary here?)
    prev_readers = prev_readers | WRITE_INDICATOR;
    //Stop looping if the lock owner by a reader
    while ((prev_readers & READER_MASKS) == 0 && (prev_readers & LOCK_INDICATOR) == 0) {
        if (readers->compare_exchange_weak(prev_readers, WRITE_INDICATOR | LOCK_INDICATOR | (prev_readers & WAITING_READERS_MASK), std::memory_order_acq_rel)) {
            // we've won the race
            _writer_wait = false; //I do not think it is necessary
            return;
        }
    }

    //There were some active readers
    //Wait for last active reader to wake us
    std::atomic<bool> *writer_wait = reinterpret_cast<std::atomic<bool>*>(&_writer_wait);
    thread::wait_until( [writer_wait] {
        return !writer_wait->load(std::memory_order_acquire);
    });

    //Acquire the lock for writing by setting the LOCK_INDICATOR and WRITE_INDICATOR bits
    readers->fetch_or(LOCK_INDICATOR | WRITE_INDICATOR, std::memory_order_acq_rel);
}

void rwlock::wake_waiting_readers(std::atomic<unsigned> *readers, unsigned waiting_readers)
{
    //TODO:In order to minimize triggering 100s of IPI wakeups to other CPUs we may
    //use a new thread::wake_many() method that would do similar logic to what
    //wake_impl() does for single thread - set status to waking for each thread, but
    //set need_reschedule or send IPI wake up once only for each relevant target CPU
    //
    //Wake pending readers one by one - stop when the count of the pending readers is 0
    //per the 16 least significant bits of _readers
    while (waiting_readers) {
        lockfree::linked_item<thread*> *read_waiter = _read_waiters.pop();
        if (!read_waiter) {
            //Even though the _read_waiters is empty, re-read the count of pending readers
            //and keep trying until it reaches 0
            waiting_readers = readers->load(std::memory_order_acquire) & WAITING_READERS_MASK;
            continue;
        }
        //Acquire the lock for reading on behalf of this pending reader by adding (READER_LOCK_INC - 1)
        //and wake its thread
        waiting_readers = readers->fetch_add(READER_LOCK_INC - 1, std::memory_order_acq_rel) & WAITING_READERS_MASK; //lock - pending
        thread *t = read_waiter->value;
        std::atomic<thread*> *rt = reinterpret_cast<std::atomic<thread*>*>(&read_waiter->value);
        rt->store(nullptr, std::memory_order_release);
        t->wake();
    }
}

void rwlock::wunlock()
{
    assert(_readers & (LOCK_INDICATOR | WRITE_INDICATOR));

    //If we are recursed then simply unlock and return
    if (_wmtx.getdepth() > 1) {
        return _wmtx.unlock();
    }

    //Allow waiting readers to acquire a lock before new writer comes in
    //or the 1st from the pending one wakes from the the sleep
    std::atomic<unsigned> *readers = reinterpret_cast<std::atomic<unsigned>*>(&_readers);
    unsigned waiting_readers = readers->fetch_and(~(LOCK_INDICATOR | WRITE_INDICATOR), std::memory_order_acq_rel) & WAITING_READERS_MASK;

    //Wake the waiting readers and acquire the lock for reading on their behalf
    wake_waiting_readers(readers, waiting_readers);

    _wmtx.unlock();
}

void rwlock::downgrade()
{
    assert(_readers & (LOCK_INDICATOR | WRITE_INDICATOR));

    std::atomic<unsigned> *readers = reinterpret_cast<std::atomic<unsigned>*>(&_readers);

    //Remove LOCK and WRITE indicators but increment readers
    unsigned prev_readers = readers->load(std::memory_order_acquire);
    while(true) {
        unsigned next_readers = (prev_readers & ~(LOCK_INDICATOR | WRITE_INDICATOR)) + READER_LOCK_INC;
        if (readers->compare_exchange_weak(prev_readers, next_readers, std::memory_order_acq_rel)) {
            break;
        }
    }
    //
    //Wake the waiting readers and acquire the lock for reading on their behalf
    wake_waiting_readers(readers, readers->load(std::memory_order_acquire) & WAITING_READERS_MASK);

    //Unlock all the way down
    for (int depth = _wmtx.getdepth(); depth > 0; depth--) {
        _wmtx.unlock();
    }
}

bool rwlock::wowned()
{
    return _wmtx.owned();
}

bool rwlock::has_readers()
{
    return _readers & READER_MASKS;
}

OSV_LIBSOLARIS_API
void rwlock_init(rwlock_t* rw)
{
    new (rw) rwlock;
}

OSV_LIBSOLARIS_API
void rwlock_destroy(rwlock_t* rw)
{
    rw->~rwlock();
}

OSV_LIBSOLARIS_API
void rw_rlock(rwlock_t* rw)
{
    rw->rlock();
}

OSV_LIBSOLARIS_API
void rw_wlock(rwlock_t* rw)
{
    rw->wlock();
}

OSV_LIBSOLARIS_API
int rw_try_rlock(rwlock_t* rw)
{
    return rw->try_rlock();
}

OSV_LIBSOLARIS_API
int rw_try_wlock(rwlock_t* rw)
{
    return rw->try_wlock();
}

OSV_LIBSOLARIS_API
void rw_runlock(rwlock_t* rw)
{
    rw->runlock();
}

OSV_LIBSOLARIS_API
void rw_wunlock(rwlock_t* rw)
{
    rw->wunlock();
}

OSV_LIBSOLARIS_API
int rw_try_upgrade(rwlock_t* rw)
{
    return rw->try_upgrade();
}

OSV_LIBSOLARIS_API
void rw_downgrade(rwlock_t* rw)
{
    rw->downgrade();
}

OSV_LIBSOLARIS_API
int rw_wowned(rwlock_t* rw)
{
    return rw->wowned();
}

int rw_has_readers(rwlock_t* rw)
{
    return rw->has_readers();
}
