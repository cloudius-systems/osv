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
#include "debug.hh"

namespace pthread_private {

    const unsigned tsd_nkeys = 100;

    __thread void* tsd[tsd_nkeys];

    mutex tsd_key_mutex;
    std::vector<bool> tsd_used_keys(tsd_nkeys);
    std::vector<void (*)(void*)> tsd_dtor(tsd_nkeys);

    class pthread {
    public:
        explicit pthread(void *(*start)(void *arg), void *arg, sigset_t sigset);
        static pthread* from_libc(pthread_t* p);
        void to_libc(pthread_t* p);
    private:
        void* _retval;
        // must be initialized last
        sched::thread _thread;
    };

    pthread::pthread(void *(*start)(void *arg), void *arg, sigset_t sigset)
        : _thread([=] {
                sigprocmask(SIG_SETMASK, &sigset, nullptr);
                _retval = start(arg);
            })
    {
    }

    pthread* pthread::from_libc(pthread_t *p)
    {
        return reinterpret_cast<pthread*>(*p);
    }

    void pthread::to_libc(pthread_t *p)
    {
        *reinterpret_cast<pthread**>(p) = this;
    }
}

using namespace pthread_private;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
        void *(*start_routine) (void *), void *arg)
{
    sigset_t sigset;
    sigprocmask(SIG_SETMASK, nullptr, &sigset);
    auto t = new pthread(start_routine, arg, sigset);
    t->to_libc(thread);
    return 0;
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
    return reinterpret_cast<pthread_t>(sched::thread::current());
}

mutex* from_libc(pthread_mutex_t* m)
{
    return reinterpret_cast<mutex*>(m);
}

int pthread_mutex_init(pthread_mutex_t* __restrict m,
        const pthread_mutexattr_t* __restrict attr)
{
    // FIXME: respect attr
    memset(m, 0, sizeof(*m));
    new (m) mutex;
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
    void wait(mutex* user_mutex);
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

void cond_var::wait(mutex* user_mutex)
{
    wait_record wr;
    with_lock(_mutex, [&] {
        _waiters.push_back(&wr);
    });
    user_mutex->unlock();
    sched::thread::wait_until([&] {
        return with_lock(_mutex, [&] {
            return !wr.t;
        });
    });
    user_mutex->lock();
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
    from_libc(cond)->wait(from_libc(mutex));
    return 0;
}

int pthread_attr_init(pthread_attr_t *attr)
{
    debug("pthread_attr_init stubbed out");
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
    // ignore - we don't have processes, so it makes no difference
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    debug(fmt("pthread_attr_setstacksize(0x%x) stubbed out") % stacksize);
    return 0;
}

int pthread_attr_setguardsize(pthread_attr_t *attr, size_t guardsize)
{
    debug(fmt("pthread_attr_setguardsize(0x%x) stubbed out") % guardsize);
    return 0;
}
