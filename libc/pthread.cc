#include "mutex.hh"
#include "sched.hh"
#include "signal.hh"
#include <pthread.h>
#include <errno.h>
#include <mutex>
#include <vector>
#include <algorithm>
#include <string.h>
#include <list>
#include "mmu.hh"
#include "debug.hh"

namespace pthread_private {

    // Because a pthread cannot pthread_join() itself (this will destroy
    // the stack currently in use), provide a separate thread which joins
    // exiting detached pthreads.
    // This is code duplication from sched::detached_thread::reaper
    // TODO: Have one class (and even one thread) do both.
    class reaper {
    public:
        reaper();
        void add_zombie(pthread_t z);
    private:
        void reap();
        mutex _mtx;
        std::list<pthread_t> _zombies;
        sched::thread _thread;
    };

    reaper::reaper() : _mtx{}, _zombies{}, _thread([=] { reap(); })
    {
        _thread.start();
    }

    void reaper::reap()
    {
        while (true) {
            with_lock(_mtx, [=] {
                sched::thread::wait_until(_mtx,
                        [=] { return !_zombies.empty(); });
                while (!_zombies.empty()) {
                    auto z = _zombies.front();
                    _zombies.pop_front();
                    pthread_join(z, nullptr);
                }
            });
        }
    }

    void reaper::add_zombie(pthread_t z)
    {
        with_lock(_mtx, [=] {
            _zombies.push_back(z);
            _thread.wake();
        });
    }

    reaper *s_reaper;

    void init_detached_pthreads_reaper()
    {
        s_reaper = new reaper;
    }


    const unsigned tsd_nkeys = 100;

    __thread void* tsd[tsd_nkeys];
    __thread pthread_t current_pthread;

    mutex tsd_key_mutex;
    std::vector<bool> tsd_used_keys(tsd_nkeys);
    std::vector<void (*)(void*)> tsd_dtor(tsd_nkeys);

    struct pmutex {
        pmutex() : initialized(true) {}
        // FIXME: use a data structure which supports zero-init natively
        bool initialized; // for PTHREAD_MUTEX_INITIALIZER
        mutex mtx;
    };

    class pthread {
    public:
        explicit pthread(void *(*start)(void *arg), void *arg, sigset_t sigset,
            bool detached);
        ~pthread() { free_stack(_thread.get_stack_info()); }
        static pthread* from_libc(pthread_t p);
        pthread_t to_libc();
        int join(void** retval);
        void* _retval;
        // must be initialized last
        sched::thread _thread;
    private:
        sched::thread::stack_info allocate_stack();
        void free_stack(sched::thread::stack_info si);
        sched::thread::attr attributes();
    };

    struct thread_attr {
        void* stack_begin;
        size_t stack_size;
        bool detached;
        thread_attr() : detached(false) { }
    };

    pthread::pthread(void *(*start)(void *arg), void *arg, sigset_t sigset,
            bool detached)
            : _thread([=] {
                current_pthread = to_libc();
                sigprocmask(SIG_SETMASK, &sigset, nullptr);
                _retval = start(arg);
                if (detached) {
                    // It would have been nice to just call pthread_join here,
                    // but this would be messy because we'll free the stack we
                    // are using now... So do this from another thread.
                    pthread_private::s_reaper->add_zombie(current_pthread);
                }
            }, attributes())
    {
        _thread.start();
    }

    sched::thread::attr pthread::attributes()
    {
        sched::thread::attr a;
        a.stack = allocate_stack();
        return a;
    }

    sched::thread::stack_info pthread::allocate_stack()
    {
        size_t size = 1024*1024;
        void *addr = mmu::map_anon(nullptr, size, true, mmu::perm_rw);
        return { addr, size };
    }

    void pthread::free_stack(sched::thread::stack_info si)
    {
        mmu::unmap(si.begin, si.size);
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
    bool detached = attr && pthread_private::from_libc(attr)->detached;
    auto t = new pthread(start_routine, arg, sigset, detached);
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

mutex* from_libc(pthread_mutex_t* m)
{
    auto p = reinterpret_cast<pmutex*>(m);
    if (!p->initialized) {
        new (p) pmutex;
    }
    return &p->mtx;
}

int pthread_mutex_init(pthread_mutex_t* __restrict m,
        const pthread_mutexattr_t* __restrict attr)
{
    // FIXME: respect attr
    new (m) pmutex;
    return 0;
}

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

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    from_libc(m)->unlock();
    return 0;
}

int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset)
{
    return sigprocmask(how, set, oldset);
}

class cond_var {
public:
    int wait(mutex* user_mutex, sched::timer* tmr = nullptr);
    void wake_one();
    void wake_all();
private:
    struct wait_record {
        explicit wait_record() : t(sched::thread::current()) {}
        sched::thread* t;
    };
private:
    mutex _mutex;
    std::list<wait_record*> _waiters;
};

cond_var* from_libc(pthread_cond_t* cond)
{
    return reinterpret_cast<cond_var*>(cond);
}

int cond_var::wait(mutex* user_mutex, sched::timer* tmr)
{
    int ret = 0;
    wait_record wr;
    with_lock(_mutex, [&] {
        _waiters.push_back(&wr);
    });
    user_mutex->unlock();
    sched::thread::wait_until([&] {
        return (tmr && tmr->expired()) || !wr.t;
    });
    if (tmr && tmr->expired()) {
        with_lock(_mutex, [&] {
            auto p = std::find(_waiters.begin(), _waiters.end(), &wr); // FIXME: O(1)
            if (p != _waiters.end()) {
                _waiters.erase(p);
            }
        });
        ret = ETIMEDOUT;
    }
    user_mutex->lock();
    return ret;
}

void cond_var::wake_one()
{
    with_lock(_mutex, [&] {
        if (!_waiters.empty()) {
            auto wr = _waiters.front();
            _waiters.pop_front();
            auto t = wr->t;
            wr->t = nullptr;
            t->wake();
        }
    });
}

void cond_var::wake_all()
{
    with_lock(_mutex, [&] {
        while (!_waiters.empty()) {
            auto wr = _waiters.front();
            _waiters.pop_front();
            auto t = wr->t;
            wr->t = nullptr;
            t->wake();
        }
    });
}

int pthread_cond_init(pthread_cond_t *__restrict cond,
       const pthread_condattr_t *__restrict attr)
{
    static_assert(sizeof(cond_var) <= sizeof(*cond), "cond_var overflow");
    new (cond) cond_var;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond)
{
    from_libc(cond)->~cond_var();
    return 0;
}

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
    tmr.set(u64(ts->tv_sec) * 1000000000 + ts->tv_nsec);
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

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    debug(fmt("pthread_attr_setstacksize(0x%x) stubbed out\n") % stacksize);
    return 0;
}

int pthread_attr_setguardsize(pthread_attr_t *attr, size_t guardsize)
{
    debug(fmt("pthread_attr_setguardsize(0x%x) stubbed out\n") % guardsize);
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
    debug(fmt("pthread_setcancelstate stubbed out\n"));
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
    debug("pthread_cancel stubbed out\n");
    return ESRCH;
}
