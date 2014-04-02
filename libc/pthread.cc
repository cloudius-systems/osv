/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/sched.hh>
#include "signal.hh"
#include <pthread.h>
#include <errno.h>
#include <mutex>
#include <vector>
#include <algorithm>
#include <string.h>
#include <list>

#ifndef AARCH64_PORT_STUB
#include <osv/mmu.hh>
#endif /* !AARCH64_PORT_STUB */

#include <osv/debug.hh>
#include <osv/prio.hh>

#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/stubbing.hh>
#include <osv/lazy_indirect.hh>

#include <api/time.h>

namespace pthread_private {

    const unsigned tsd_nkeys = 100;

    __thread void* tsd[tsd_nkeys];
    __thread pthread_t current_pthread;

    __attribute__ ((init_priority ((int)init_prio::pthread))) mutex tsd_key_mutex;
    __attribute__ ((init_priority ((int)init_prio::pthread))) std::vector<bool>
                                          tsd_used_keys(tsd_nkeys);
    __attribute__ ((init_priority ((int)init_prio::pthread)))
                  std::vector<void (*)(void*)> tsd_dtor(tsd_nkeys);

    struct thread_attr;

    class pthread {
    public:
        explicit pthread(void *(*start)(void *arg), void *arg, sigset_t sigset,
            const thread_attr* attr);
        static pthread* from_libc(pthread_t p);
        pthread_t to_libc();
        int join(void** retval);
        void* _retval;
        // must be initialized last
        sched::thread _thread;
    private:
        sched::thread::stack_info allocate_stack(thread_attr attr);
        static void free_stack(sched::thread::stack_info si);
        sched::thread::attr attributes(thread_attr attr);
    };

    struct thread_attr {
        void* stack_begin;
        size_t stack_size;
        size_t guard_size;
        bool detached;
        thread_attr() : stack_begin{}, stack_size{1<<20}, guard_size{4096}, detached{false} {}
    };

    pthread::pthread(void *(*start)(void *arg), void *arg, sigset_t sigset,
                     const thread_attr* attr)
            : _thread([=] {
                current_pthread = to_libc();
                sigprocmask(SIG_SETMASK, &sigset, nullptr);
                _retval = start(arg);
            }, attributes(attr ? *attr : thread_attr()))
    {
        _thread.set_cleanup([=] { delete this; });
        _thread.start();
    }

    sched::thread::attr pthread::attributes(thread_attr attr)
    {
        sched::thread::attr a;
        a.stack(allocate_stack(attr));
        a.detached(attr.detached);
        return a;
    }

    sched::thread::stack_info pthread::allocate_stack(thread_attr attr)
    {
        if (attr.stack_begin) {
            return {attr.stack_begin, attr.stack_size};
        }
        size_t size = attr.stack_size;
#ifdef AARCH64_PORT_STUB
        size = (size + (0xfff)) & (~0xfffull);
        void *addr = malloc(size);
#else  /* !AARCH64_PORT_STUB */
        void *addr = mmu::map_anon(nullptr, size, mmu::mmap_populate, mmu::perm_rw);
        mmu::mprotect(addr, attr.guard_size, 0);
#endif /* !AARCH64_PORT_STUB */
        sched::thread::stack_info si{addr, size};
        si.deleter = free_stack;
        return si;
    }

    void pthread::free_stack(sched::thread::stack_info si)
    {
#ifdef AARCH64_PORT_STUB
        free(si.begin);
#else  /* !AARCH64_PORT_STUB */
        mmu::munmap(si.begin, si.size);
#endif /* !AARCH64_PORT_STUB */
    }

    int pthread::join(void** retval)
    {
        _thread.join();
        if (retval) {
            *retval = _retval;
        }
        return 0;
    }

    pthread* pthread::from_libc(pthread_t p)
    {
        return reinterpret_cast<pthread*>(p);
    }

    static_assert(sizeof(thread_attr) <= sizeof(pthread_attr_t),
            "thread_attr too big");

    pthread_t pthread::to_libc()
    {
        return reinterpret_cast<pthread_t>(this);
    }

    thread_attr* from_libc(pthread_attr_t* a)
    {
        return reinterpret_cast<thread_attr*>(a);
    }

    const thread_attr* from_libc(const pthread_attr_t* a)
    {
        return reinterpret_cast<const thread_attr*>(a);
    }
}

using namespace pthread_private;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
        void *(*start_routine) (void *), void *arg)
{
    sigset_t sigset;
    sigprocmask(SIG_SETMASK, nullptr, &sigset);
    auto t = new pthread(start_routine, arg, sigset, from_libc(attr));
    *thread = t->to_libc();
    return 0;
}

int pthread_join(pthread_t thread, void** retval)
{
    int ret = pthread::from_libc(thread)->join(retval);
    delete(pthread::from_libc(thread));
    return ret;
}

int pthread_key_create(pthread_key_t* key, void (*dtor)(void*))
{
    std::lock_guard<mutex> guard(tsd_key_mutex);
    auto p = std::find(tsd_used_keys.begin(), tsd_used_keys.end(), false);
    if (p == tsd_used_keys.end()) {
        return ENOMEM;
    }
    *p = true;
    *key = p - tsd_used_keys.begin();
    tsd_dtor[*key] = dtor;
    return 0;
}

extern "C"
int __pthread_key_create(pthread_key_t* key, void (*dtor)(void*))
    __attribute__((alias("pthread_key_create")));

int pthread_key_delete(pthread_key_t key)
{
    WARN_STUBBED();
    return EINVAL;
}

void* pthread_getspecific(pthread_key_t key)
{
    return tsd[key];
}

int pthread_setspecific(pthread_key_t key, const void* value)
{
    tsd[key] = const_cast<void*>(value);
    return 0;
}

pthread_t pthread_self()
{
    return current_pthread;
}

int pthread_getcpuclockid(pthread_t thread, clockid_t *clock_id)
{
    if (clock_id) {
        pthread *p = pthread::from_libc(thread);
        auto id = p->_thread.id();
        *clock_id = id + _OSV_CLOCK_SLOTS;
    }
    return 0;
}

#ifdef LOCKFREE_MUTEX
typedef lazy_indirect<mutex> indirect_mutex;
static_assert(sizeof(indirect_mutex) <= sizeof(pthread_mutex_t), "mutex overflow");
static_assert(sizeof(indirect_mutex) <= 16, "pthread_mutex_t not zeroed at byte 16");
int pthread_mutex_init(pthread_mutex_t* __restrict m,
        const pthread_mutexattr_t* __restrict attr)
{
    // FIXME: respect attr
    new (m) indirect_mutex;
    return 0;
}
int pthread_mutex_destroy(pthread_mutex_t *m)
{
    reinterpret_cast<indirect_mutex*>(m)->~indirect_mutex();
    return 0;
}
mutex* from_libc(pthread_mutex_t* m)
{
    return reinterpret_cast<indirect_mutex*>(m)->get();
}

#else
static_assert(sizeof(mutex) <= sizeof(pthread_mutex_t), "mutex overflow");

mutex* from_libc(pthread_mutex_t* m)
{
    return reinterpret_cast<mutex*>(m);
}

int pthread_mutex_init(pthread_mutex_t* __restrict m,
        const pthread_mutexattr_t* __restrict attr)
{
    // FIXME: respect attr
    new (m) mutex;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
    from_libc(m)->~mutex();
    return 0;
}
#endif

int pthread_mutex_lock(pthread_mutex_t *m)
{
    from_libc(m)->lock();
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
    if (!from_libc(m)->try_lock()) {
        return -EBUSY;
    }
    return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *m,
        const struct timespec *abs_timeout)
{
    WARN_STUBBED();
    return EINVAL;
}


int pthread_mutex_unlock(pthread_mutex_t *m)
{
    from_libc(m)->unlock();
    return 0;
}

int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset)
{
    return sigprocmask(how, set, oldset);
}

#ifdef LOCKFREE_MUTEX
struct pthread_condvar {
    condvar cond;
    clockid_t clock {CLOCK_REALTIME};
};
typedef lazy_indirect<pthread_condvar> indirect_condvar;
static_assert(sizeof(indirect_condvar) < sizeof(pthread_cond_t), "condvar overflow");
int pthread_cond_init(pthread_cond_t* __restrict c,
        const pthread_condattr_t* __restrict attr)
{
    new (c) indirect_condvar;
    // There's not much that attr can say. OSv doesn't have processes so the
    // pshared attribute is irrelevant. All that remains is the clock, and
    // since this can only specify CLOCK_REALTIME or CLOCK_MONOTONIC, a
    // single byte in pthread_condattr_t is enough for us.
    if (attr && *reinterpret_cast<const char*>(attr)) {
        reinterpret_cast<indirect_condvar*>(c)->get()->clock = CLOCK_MONOTONIC;
    }
    return 0;
}
int pthread_cond_destroy(pthread_cond_t *c)
{
    reinterpret_cast<indirect_condvar*>(c)->~indirect_condvar();
    return 0;
}
condvar* from_libc(pthread_cond_t* c)
{
    return &(reinterpret_cast<indirect_condvar*>(c)->get()->cond);
}

#else
condvar* from_libc(pthread_cond_t* cond)
{
    return reinterpret_cast<condvar*>(cond);
}

int pthread_cond_init(pthread_cond_t *__restrict cond,
       const pthread_condattr_t *__restrict attr)
{
    static_assert(sizeof(condvar) <= sizeof(*cond), "cond_var overflow");
    memset(cond, 0, sizeof(*cond));
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond)
{
    return 0;
}
#endif

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    from_libc(cond)->wake_all();
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    from_libc(cond)->wake_one();
    return 0;
}

int pthread_cond_wait(pthread_cond_t *__restrict cond,
       pthread_mutex_t *__restrict mutex)
{
    return from_libc(cond)->wait(from_libc(mutex));
}

int pthread_cond_timedwait(pthread_cond_t *__restrict cond,
                           pthread_mutex_t *__restrict mutex,
                           const struct timespec* __restrict ts)
{
    sched::timer tmr(*sched::thread::current());
    switch(reinterpret_cast<indirect_condvar*>(cond)->get()->clock) {
    case CLOCK_REALTIME:
        tmr.set(osv::clock::wall::time_point(
                std::chrono::seconds(ts->tv_sec) +
                std::chrono::nanoseconds(ts->tv_nsec)));
        break;
    case CLOCK_MONOTONIC:
        tmr.set(osv::clock::uptime::time_point(
                std::chrono::seconds(ts->tv_sec) +
                std::chrono::nanoseconds(ts->tv_nsec)));
        break;
    default:
        assert(0); // pthread_cond_init() will never allow this case
    }
    return from_libc(cond)->wait(from_libc(mutex), &tmr);
}

int pthread_attr_init(pthread_attr_t *attr)
{
    new (attr) thread_attr;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    return 0;
}

int pthread_getattr_np(pthread_t thread, pthread_attr_t *attr)
{
    auto t = pthread::from_libc(thread);
    auto a = from_libc(attr);
    a->stack_begin = t->_thread.get_stack_info().begin;
    a->stack_size = t->_thread.get_stack_info().size;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
    auto a = from_libc(attr);
    a->detached = (detachstate == PTHREAD_CREATE_DETACHED);
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *__restrict attr,
                                  size_t * stacksize)
{
    auto a = from_libc(attr);
    *stacksize = a->stack_size;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    from_libc(attr)->stack_size = stacksize;
    return 0;
}

int pthread_attr_setguardsize(pthread_attr_t *attr, size_t guardsize)
{
    from_libc(attr)->guard_size = guardsize;
    return 0;
}

int pthread_attr_getstack(const pthread_attr_t * __restrict attr,
                                void **stackaddr, size_t *stacksize)
{
    auto a = from_libc(attr);
    *stackaddr = a->stack_begin;
    *stacksize = a->stack_size;
    return 0;
}

int pthread_setcancelstate(int state, int *oldstate)
{
    WARN_STUBBED();
    return 0;
}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    // In Linux (the target ABI we're trying to emulate, PTHREAD_ONCE_INIT
    // is 0. Our implementation sets it to 1 when intialization is in progress
    // (so other threads calling the same pthread_once know to block), and
    // to 2 when initialization has alread completed.
    // The performance pthread_once isn't critical, so let's go with a simple
    // implementation of one shared mutex and wait queue for all once_control
    // (don't worry, the mutual exclusion is just on access to the waiter
    // list, not on running init_routine()).

    static mutex m;
    static struct waiter {
        sched::thread *thread;
        pthread_once_t *once_control;
        struct waiter *next;
    } *waiterlist = nullptr;

    m.lock();
    if (*once_control == 2) {
        // initialization has already completed - return immediately.
        m.unlock();
        return 0;
    } else if (*once_control == 1) {
        // initialization of this once_control is in progress in another
        // thread, so wait until it completes.
        struct waiter *w = (struct waiter *) malloc(sizeof(struct waiter));
        w->thread = sched::thread::current();
        w->once_control = once_control;
        w->next = waiterlist;
        waiterlist = w;
        sched::thread::wait_until(m, [=] { return *once_control != 1; });
        if (*once_control == 2) {
            m.unlock();
            return 0;
        }
        // If we're still here, it's the corner case that we waited for
        // another thread that was doing the initialization, but that thread
        // was canceled once_control is back to 0, and now we need to
        // initialize in this thread. Fall through to the code below with the
        // lock still taken.
    } else if (*once_control != 0) {
        // Unexpected value in once_control. Barf.
        m.unlock();
        return EINVAL;
    }

    // mark that we're initializing, and run the initialization routine.
    *once_control = 1;
    m.unlock();
    init_routine();
    // TODO: if init_routine() was canceled, return once_control back to 0!
    m.lock();
    *once_control = 2;
    // wake up any other threads waiting for our initialization. We need to
    // do this with the lock taken, as we are touching the waitlist.
    for (struct waiter **p = &waiterlist; *p; p = &((*p)->next)) {
        if ((*p)->once_control == once_control ) {
            (*p)->thread->wake();
            struct waiter *save = *p;
            *p = (*p)->next;
            free(save);
            if (!*p) break;
        }
    }
    m.unlock();
    return 0;
}

// libstdc++ checks whether threads are compiled in using its
// __gthread_active_p(), which (when compiled on Linux) just verifies
// that pthread_cancel() is available. So we need it available, even
// we don't intend to actually use it.
int pthread_cancel(pthread_t thread)
{
    WARN_STUBBED();
    return ESRCH;
}

int pthread_detach(pthread_t thread)
{
    pthread* p = pthread::from_libc(thread);
    p->_thread.detach();
    return 0;
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
    return t1 == t2;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    WARN_STUBBED();
    return ENOMEM;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    WARN_STUBBED();
    return EINVAL;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)
{
    WARN_STUBBED();
    return EINVAL;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    WARN_STUBBED();
    return EINVAL;
}

int pthread_condattr_init(pthread_condattr_t *attr)
{
    // We assume there's room for at least one byte in pthread_condattr_t
    // and use this single byte to specify the clockid. The default is 0
    // (which we take to signify CLOCK_REALTIME).
    *reinterpret_cast<char*>(attr) = 0;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *)
{
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clockid)
{
    char byte;
    switch (clockid) {
    case CLOCK_REALTIME:
        byte = 0;
        break;
    case CLOCK_MONOTONIC:
        byte = 1;
        break;
    default:
        return EINVAL;
    }
    *reinterpret_cast<char*>(attr) = byte;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *__restrict attr,
        clockid_t *__restrict clockid)
{
    if (*reinterpret_cast<const char*>(attr)) {
        *clockid = CLOCK_MONOTONIC;
    } else {
        *clockid = CLOCK_REALTIME;
    }
    return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *, int)
{
    WARN_STUBBED();
    return EINVAL;
}


int pthread_condattr_getpshared(const pthread_condattr_t *__restrict, int *__restrict)
{
    WARN_STUBBED();
    return EINVAL;
}

void pthread_exit(void *retval)
{
    WARN_STUBBED();
    abort();
}

int sched_get_priority_max(int policy)
{
    WARN_STUBBED();
    return EINVAL;
}

int sched_get_priority_min(int policy)
{
    WARN_STUBBED();
    return EINVAL;
}

int pthread_setschedparam(pthread_t thread, int policy,
        const struct sched_param *param)
{
    WARN_STUBBED();
    return EINVAL;
}

int pthread_getschedparam(pthread_t thread, int *policy,
        struct sched_param *param)
{
    WARN_STUBBED();
    return EINVAL;
}

int pthread_kill(pthread_t thread, int sig)
{
    WARN_STUBBED();

    return EINVAL;
}

int pthread_setname_np(pthread_t p, const char* name)
{
    if (strlen(name) > 16) {
        return ERANGE;
    }
    pthread::from_libc(p)->_thread.set_name(name);
    return 0;
}
