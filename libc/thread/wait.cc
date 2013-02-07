
struct sem_waiter {
    struct sem_waiter*	prev;
    struct sem_waiter*	next;
    sched::thread*	thread;
};

extern "C" void sem_wait(mutex_t *mutex)
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

extern "C" void sem_wake(sem_t *sem)
{
    spin_lock(&mutex->_wait_lock);
    if (mutex->_wait_list.first)
        mutex->_wait_list.first->thread->wake();
    spin_unlock(&mutex->_wait_lock);
}
