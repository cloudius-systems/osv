#include <sys/time.h>
#include "drivers/clock.hh"

int gettimeofday(struct timeval* tv, struct timezone* tz)
{
    if (!tv) {
        return 0;
    }
    u64 time = clock::get()->time();
    auto sec = time / 1000000000;
    auto nsec = time % 1000000000;
    tv->tv_sec = sec;
    tv->tv_usec = nsec / 1000;
    return 0;
}
