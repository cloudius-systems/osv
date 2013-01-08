#include "mutex.hh"
#include "sched.hh"
#include "signal.hh"
#include <pthread.h>
#include <errno.h>
#include <mutex>
#include <vector>
#include <algorithm>
#include <string.h>

namespace pthread_private {

    const unsigned tsd_nkeys = 100;

    __thread void* tsd[tsd_nkeys];

    mutex tsd_key_mutex;
    std::vector<bool> tsd_used_keys(tsd_nkeys);
    std::vector<void (*)(void*)> tsd_dtor(tsd_nkeys);
}

using namespace pthread_private;

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

int pthread_setspecific(pthread_key_t key, void* value)
{
    tsd[key] = value;
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
