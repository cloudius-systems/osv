#ifndef MUTEX_H_
#define MUTEX_H_

#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Spin lock. Use mutex instead, except where impossible:

typedef struct spinlock {
    bool _lock;
#ifdef __cplusplus
    // additional convenience methods for C++
    inline constexpr spinlock() : _lock(false) { }
    inline void lock();
    inline void unlock();
#endif
} spinlock_t;

static inline void spinlock_init(spinlock_t *sl)
{
    sl->_lock = false;
}
void spin_lock(spinlock_t *sl);
void spin_unlock(spinlock_t *sl);

#ifdef __cplusplus
void spinlock::lock()
{
    spin_lock(this);
}
void spinlock::unlock()
{
    spin_unlock(this);
}
#endif


// Mutex:

typedef struct mutex {
    spinlock_t _wait_lock;
    unsigned _depth;
    void *_owner;
    struct wait_list {
        struct waiter *first;
        struct waiter *last;
    } _wait_list;
#ifdef __cplusplus
    // additional convenience methods for C++
    inline mutex();
    inline ~mutex();
    inline void lock();
    inline bool try_lock();
    inline void unlock();
    inline bool owned();
    // getdepth() should only be used by the thread holding the lock
    inline unsigned int getdepth() const { return _depth; }
#endif
} mutex_t;

#define MUTEX_INITIALIZER	{}

void mutex_lock(mutex_t* m);
bool mutex_trylock(mutex_t* m);
void mutex_unlock(mutex_t* m);
/* Is owned by current thread */
bool mutex_owned(mutex_t* m);

static inline void mutex_init(mutex_t* m)
{
    m->_depth = 0;
    m->_owner = 0;
    m->_wait_list.first = 0;
    m->_wait_list.last = 0;
    spinlock_init(&m->_wait_lock);
}

static inline void mutex_destroy(mutex_t* m)
{
}

#ifdef __cplusplus
}

mutex::mutex()
{
    mutex_init(this);
}
mutex::~mutex()
{
    mutex_destroy(this);
}
void mutex::lock()
{
    mutex_lock(this);
}
bool mutex::try_lock()
{
    return mutex_trylock(this);
}
void mutex::unlock()
{
    return mutex_unlock(this);
}
bool mutex::owned()
{
    return mutex_owned(this);
}

#include <mutex>

template <class Lock, class Func>
auto with_lock(Lock& lock, Func func) -> decltype(func())
{
    std::lock_guard<Lock> guard(lock);
    return func();
}
#endif

#endif /* MUTEX_H_ */
