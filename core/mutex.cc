
#include "mutex.hh"
#include <sched.hh>

struct waiter {
    struct waiter*	prev;
    struct waiter*	next;
    sched::thread*	thread;
};

extern "C" void mutex_wait(mutex_t *mutex)
{
    struct waiter w;

    w.thread = sched::thread::current();

    spin_lock(&mutex->_wait_lock);
    if (!mutex->_wait_list.first) {
        mutex->_wait_list.first = &w;
    } else {
        mutex->_wait_list.last->next = &w;
        w.prev = mutex->_wait_list.last;
    }
    mutex->_wait_list.last = &w;
    spin_unlock(&mutex->_wait_lock);

    sched::thread::wait_until([=] {
        return !mutex->_locked && mutex->_wait_list.first == &w;
    });

    spin_lock(&mutex->_wait_lock);
    if (mutex->_wait_list.first == &w)
        mutex->_wait_list.first = w.next;
    else
        w.prev->next = w.next;

    if (mutex->_wait_list.last == &w)
        mutex->_wait_list.last = w.prev;
    spin_unlock(&mutex->_wait_lock);
}

extern "C" void mutex_lock(mutex_t *mutex)
{
    while (__sync_lock_test_and_set(&mutex->_locked, 1)) {
        mutex_wait(mutex);
    }
}

extern "C" bool mutex_trylock(mutex_t *mutex)
{
    return !__sync_lock_test_and_set(&mutex->_locked, 1);
}

extern "C" void mutex_unlock(mutex_t *mutex)
{
    __sync_lock_release(&mutex->_locked, 0);

    spin_lock(&mutex->_wait_lock);
    if (mutex->_wait_list.first)
        mutex->_wait_list.first->thread->wake();
    spin_unlock(&mutex->_wait_lock);
}

void mutex::lock()
{
    mutex_lock(&_mutex);
}

bool mutex::try_lock()
{
    return mutex_trylock(&_mutex);
}

void mutex::unlock()
{
    mutex_unlock(&_mutex);
}

void spinlock::lock()
{
    spin_lock(&_lock);
}

void spinlock::unlock()
{
    spin_unlock(&_lock);
}
