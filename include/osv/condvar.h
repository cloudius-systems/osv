#ifndef CONDVAR_H_
#define CONDVAR_H_

#include <stdint.h>

#include <osv/mutex.h>

#ifdef __cplusplus

// While the condvar type can be used in C, to C++ code we offer additional
// convenience methods and functions, for which we need these headers:
#include "sched.hh"

#endif

// Note: To be useful for implementing pthread's condition variables, the
// condvar_t structure doesn't need any special initialization beyond zero
// initialization (note PTHREAD_COND_INITIALIZER is all zeros). For this
// to work, mutex_t which we use below, should also be ok with zero
// initialization - and in the current implementation it indeed is.
//
// Moreover, we also don't have a de-initialization function, as there
// is no memory dynamically allocated, nor is there anything meaningful
// to do when the waiter list is not empty.

struct ccondvar_waiter;

typedef struct condvar {
    mutex_t m;
    struct {
        // A FIFO queue of waiters - a linked list from oldest (next in line
        // to be woken) towards newest. The ccondvar_wait structures
        // themselves are held on the stack of the waiting thread - so no
        // dynamic memory allocation is needed for this list.
        ccondvar_waiter *oldest;
        ccondvar_waiter *newest;
    } waiters_fifo;

#ifdef __cplusplus
    // In C++, for convenience also provide methods.
    condvar() { memset(this, 0, sizeof *this); }
    inline int wait(mutex_t *user_mutex, sched::timer *tmr = nullptr);
    inline void wake_one();
    inline void wake_all();
    template <class Pred>
    void wait_until(mutex& mtx, Pred pred);
#endif
} condvar_t;

#define CONDVAR_INITIALIZER	{}


#ifdef __cplusplus
extern "C" {
#endif

int condvar_wait(condvar_t *condvar, mutex_t* user_mutex, uint64_t expiration);
void condvar_wake_one(condvar_t *condvar);
void condvar_wake_all(condvar_t *condvar);

#ifdef __cplusplus
}

// additional convenience functions for C++
int condvar_wait(condvar_t *condvar, mutex_t *user_mutex, sched::timer *tmr);
int condvar_t::wait(mutex_t *user_mutex, sched::timer *tmr) {
    return condvar_wait(this, user_mutex, tmr);
}
void condvar_t::wake_one() {
    return condvar_wake_one(this);
}
void condvar_t::wake_all() {
    return condvar_wake_all(this);
}

template <class Pred>
inline
void condvar::wait_until(mutex& mtx, Pred pred)
{
    while (!pred()) {
        wait(&mtx);
    }
}


#endif

#endif /* MUTEX_H_ */
