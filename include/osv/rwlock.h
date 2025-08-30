/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 * Copyright (C) 2025 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __OSV_RWLOCK_H__
#define __OSV_RWLOCK_H__

#include <osv/mutex.h>
#include <sys/cdefs.h>
#include <osv/waitqueue.hh>

#define RWLOCK_INITIALIZER {}

#define LOCKFREE_QUEUE_MPSC_ALIGN void*
#define LOCKFREE_QUEUE_MPSC_SIZE  16

#ifdef __cplusplus
#include <lockfree/queue-mpsc.hh>

namespace sched {
    class thread;
}

static_assert(sizeof(lockfree::queue_mpsc<lockfree::linked_item<sched::thread*>>) == LOCKFREE_QUEUE_MPSC_SIZE,
         "LOCKFREE_QUEUE_MPSC_SIZE should match size of lockfree::queue_mpsc");
static_assert(alignof(lockfree::queue_mpsc<lockfree::linked_item<sched::thread*>>) == alignof(LOCKFREE_QUEUE_MPSC_ALIGN),
         "LOCKFREE_QUEUE_MPSC_ALIGN should match alignment of lockfree::queue_mpsc");

class rwlock;

// an rwlock pretending it is an ordinary lock for
// WITH_LOCK() and friends.  Taking the lock acquires
// it for reading.
//
// example: WITH_LOCK(my_rwlock.for_read()) { ... }
class rwlock_for_read {
private:
    rwlock_for_read() {}
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
    rwlock_for_write() {}
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

    // has_readers() does not check whether this thread holds the read lock,
    // but rather whether any thread holds it. Therefore, this function is
    // inherently prone to races, and should be avoided.
    bool has_readers();

private:
    friend class rwlock_for_read;
    friend class rwlock_for_write;

    void wake_pending_readers(unsigned pending_readers);

    //TODO: Consider replacing with lockfree::unordered_queue_mpsc which is
    //supposedly faster but uses more memory (has to be CACHELINE_ALIGNED)
    lockfree::queue_mpsc<lockfree::linked_item<sched::thread*>> _read_waiters;
#else
    //For C
    union {
        char forsize[LOCKFREE_QUEUE_MPSC_SIZE];
        LOCKFREE_QUEUE_MPSC_ALIGN foralignment;
    };
#endif // __cplusplus

    mutex_t _wmtx;
#ifdef __cplusplus
    std::atomic<unsigned> _readers; //TODO: Consider expanding to a 64-bit long type to increase maximum number of owning readers and pending readers
    std::atomic<bool> _writer_wait;
#else
    unsigned _readers_for_size;
    bool _writer_wait_for_size;
#endif
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
int rw_wowned(rwlock_t* rw);
int rw_has_readers(rwlock_t* rw);
__END_DECLS

#endif // !__RWLOCK_H__
