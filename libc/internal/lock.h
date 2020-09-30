#include <osv/mutex.h>

#define LOCK(x) mutex_lock(&(x))
#define UNLOCK(x) mutex_unlock(&(x))
