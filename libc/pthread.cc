#include "mutex.hh"
#include <pthread.h>
#include <errno.h>
#include <mutex>
#include <vector>
#include <algorithm>

namespace pthread_private {

    const unsigned tsd_nkeys = 100;

    __thread void* tsd[tsd_nkeys];

    mutex tsd_key_mutex;
    std::vector<bool> tsd_used_keys(tsd_nkeys);
}

using namespace pthread_private;

pthread_key_t pthread_key_create()
{
    std::lock_guard<mutex> guard(tsd_key_mutex);
    auto p = std::find(tsd_used_keys.begin(), tsd_used_keys.end(), false);
    if (p == tsd_used_keys.end()) {
        return ENOMEM;
    }
    *p = true;
    return p - tsd_used_keys.begin();
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
