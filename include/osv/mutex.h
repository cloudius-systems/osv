#ifndef MUTEX_H_
#define MUTEX_H_

#include <stdbool.h>
#include <string.h>
#include <stdint.h>

// To use the spin-lock-based mutex instead of lockfree::mutex everywhere,
// change #define LOCKFREE_MUTEX here to #undef.
#define LOCKFREE_MUTEX

#ifdef LOCKFREE_MUTEX
#define LOCKFREE_MUTEX_ALIGN void*
#define LOCKFREE_MUTEX_SIZE 40
#ifdef __cplusplus
/** C++ **/
#include <lockfree/mutex.hh>
typedef lockfree::mutex mutex;
typedef lockfree::mutex mutex_t;
static_assert(sizeof(mutex) == LOCKFREE_MUTEX_SIZE,
        "LOCKFREE_MUTEX_SIZE should match lockfree::mutex");
static_assert(alignof(mutex) == alignof(LOCKFREE_MUTEX_ALIGN),
        "LOCKFREE_MUTEX_ALIGN should match alignment of lockfree::mutex");
static inline void mutex_lock(mutex_t* m)
{
    m->lock();
}
static inline bool mutex_trylock(mutex_t* m)
{
    return m->try_lock();
}
static inline void mutex_unlock(mutex_t* m)
{
    m->unlock();
}
static inline bool mutex_owned(mutex_t* m)
{
    return m->owned();
}
#else
/** C **/
typedef struct mutex {
    union {
        char forsize[LOCKFREE_MUTEX_SIZE];
        LOCKFREE_MUTEX_ALIGN foralignment;
    };
} mutex_t;
void lockfree_mutex_lock(void *m);
void lockfree_mutex_unlock(void *m);
bool lockfree_mutex_try_lock(void *m);
bool lockfree_mutex_owned(void *m);
static inline void mutex_lock(mutex_t *m) { lockfree_mutex_lock(m); }
static inline void mutex_unlock(mutex_t *m) { lockfree_mutex_unlock(m); }
static inline bool mutex_trylock(mutex_t *m) { return lockfree_mutex_try_lock(m); }
static inline bool mutex_owned(mutex_t *m) { return lockfree_mutex_owned(m); }
#endif
/** both C and C++ code currently use these, though they should be C-only  **/
static inline void mutex_init(mutex_t* m) { memset(m, 0, sizeof(mutex_t)); }
static inline void mutex_destroy(mutex_t* m) { }
#define MUTEX_INITIALIZER   {}
#endif /* LOCKFREE_MUTEX */

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

#ifndef LOCKFREE_MUTEX
typedef struct mutex {
    struct wait_list {
        struct waiter *first;
        struct waiter *last;
    } _wait_list;
    uint32_t _hole_for_pthread_compatiblity; // pthread_mutex_t's __kind
    spinlock_t _wait_lock;
    uint16_t _depth;
    void *_owner;
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
#endif

#ifdef __cplusplus
}

#ifndef LOCKFREE_MUTEX
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
#endif

#include <mutex>
#include <cstdlib>

template <class lock_type>
struct lock_guard_for_with_lock : std::lock_guard<lock_type> {
    lock_guard_for_with_lock(lock_type& lock) : std::lock_guard<lock_type>(lock) {}
    operator bool() const { return false; }
};

#define WITH_LOCK(_wl_lock) \
    if (lock_guard_for_with_lock<decltype(_wl_lock)> _wl_lock_guard{_wl_lock}) \
        std::abort(); \
    else /* locked statement comes here */


#endif

#endif /* MUTEX_H_ */
