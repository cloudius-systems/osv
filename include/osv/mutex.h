#ifndef MUTEX_H_
#define MUTEX_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cspinlock {
    bool lock;
};

typedef struct cspinlock spinlock_t;

struct cmutex {
    bool _locked;
    struct wait_list {
        struct waiter *first;
        struct waiter *last;
    } _wait_list;
    spinlock_t _wait_lock;
};

typedef struct cmutex mutex_t;


void mutex_lock(mutex_t* m);
bool mutex_trylock(mutex_t* m);
void mutex_unlock(mutex_t* m);

inline void spin_lock(spinlock_t *sl)
{
    while (__sync_lock_test_and_set(&sl->lock, 1))
        ;
}

inline void spin_unlock(spinlock_t *sl)
{
    __sync_lock_release(&sl->lock, 0);
}

#ifdef __cplusplus
}
#endif

#endif /* MUTEX_H_ */
