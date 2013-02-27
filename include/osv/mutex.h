#ifndef MUTEX_H_
#define MUTEX_H_

#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cspinlock {
    bool lock;
};

typedef struct cspinlock spinlock_t;

static inline void spinlock_init(spinlock_t *sl)
{
    sl->lock = false;
}

struct cmutex {
    spinlock_t _wait_lock;
    unsigned _depth;
    void *_owner;
    struct wait_list {
        struct waiter *first;
        struct waiter *last;
    } _wait_list;
};

typedef struct cmutex mutex_t;

#define MUTEX_INITIALIZER	{}

void mutex_lock(mutex_t* m);
bool mutex_trylock(mutex_t* m);
void mutex_unlock(mutex_t* m);
/* Is owned by current thread */
bool mutex_owned(mutex_t* m);

static __always_inline void mutex_init(mutex_t* m)
{
    m->_depth = 0;
    m->_owner = 0;
    m->_wait_list.first = 0;
    m->_wait_list.last = 0;
    spinlock_init(&m->_wait_lock);
}

static __always_inline void mutex_destroy(mutex_t* m)
{
}

void spin_lock(spinlock_t *sl);
void spin_unlock(spinlock_t *sl);

#ifdef __cplusplus
}
#endif

#endif /* MUTEX_H_ */
