#include <osv/mutex.h>
#include <sched.hh>
#include "arch.hh"
#include <pthread.h>
#include <cassert>
#include "osv/trace.hh"

#ifndef LOCKFREE_MUTEX
static_assert(sizeof(mutex) <= sizeof(pthread_mutex_t), "mutex too big");
static_assert(offsetof(mutex, _hole_for_pthread_compatiblity) == 16, "mutex hole in wrong place");

tracepoint<21001, mutex_t*, void*> trace_mutex_lock("mutex_lock", "%p at RIP=%p");
tracepoint<21002, mutex_t*, void*> trace_mutex_unlock("mutex_unlock", "%p at RIP=%p");
tracepoint<21003, mutex_t*, void*> trace_mutex_wait("mutex_lock_wait", "%p held by %p");

struct waiter {
    struct waiter* next;
    sched::thread* thread;
};
#endif


void spin_lock(spinlock_t *sl)
{
    arch::irq_disable();
    while (__sync_lock_test_and_set(&sl->_lock, 1))
        ;
}

void spin_unlock(spinlock_t *sl)
{
    __sync_lock_release(&sl->_lock, 0);
    arch::irq_enable();
}

#ifndef LOCKFREE_MUTEX
void mutex_lock(mutex_t *mutex)
{
    struct waiter w;

    trace_mutex_lock(mutex, __builtin_return_address(0));

    w.thread = sched::thread::current();
    spin_lock(&mutex->_wait_lock);
    if (!mutex->_owner || mutex->_owner == w.thread) {
        mutex->_owner = w.thread;
        ++mutex->_depth;
        spin_unlock(&mutex->_wait_lock);
        return;
    }
    if (!mutex->_wait_list.first) {
        mutex->_wait_list.first = &w;
    } else {
        mutex->_wait_list.last->next = &w;
    }
    w.next = nullptr;
    mutex->_wait_list.last = &w;
    spin_unlock(&mutex->_wait_lock);

    trace_mutex_wait(mutex, mutex->_owner);
    sched::thread::wait_until([=] {
        return mutex->_owner == w.thread;
    });

    spin_lock(&mutex->_wait_lock);
    mutex->_wait_list.first = w.next;
    if (!w.next)
        mutex->_wait_list.last = nullptr;
    spin_unlock(&mutex->_wait_lock);
}

bool mutex_trylock(mutex_t *mutex)
{
    bool ret = false;
    spin_lock(&mutex->_wait_lock);
    if (!mutex->_owner || mutex->_owner == sched::thread::current()) {
        mutex->_owner = sched::thread::current();
        ++mutex->_depth;
        ret = true;
    }
    spin_unlock(&mutex->_wait_lock);
    return ret;
}

void mutex_unlock(mutex_t *mutex)
{
    trace_mutex_unlock(mutex, __builtin_return_address(0));

    spin_lock(&mutex->_wait_lock);
    if (mutex->_depth == 1) {
        if (mutex->_wait_list.first) {
            mutex->_owner = mutex->_wait_list.first->thread;
            mutex->_wait_list.first->thread->wake();
        } else {
            mutex->_owner = nullptr;
            --mutex->_depth;
        }
    } else {
        --mutex->_depth;
    }
    spin_unlock(&mutex->_wait_lock);
}

bool mutex_owned(mutex_t* mutex)
{
    return (mutex->_owner == sched::thread::current());
}
#endif
