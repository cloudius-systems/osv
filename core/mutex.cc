#include <osv/mutex.h>
#include <sched.hh>
#include "arch.hh"

struct waiter {
    struct waiter*	next;
    sched::thread*	thread;
};

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

void mutex_lock(mutex_t *mutex)
{
    struct waiter w;

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
