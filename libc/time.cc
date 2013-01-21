#include <sys/time.h>
#include "drivers/clock.hh"
#include "sched.hh"

u64 convert(const timespec& ts)
{
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

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

int nanosleep(const struct timespec* req, struct timespec* rem)
{
    sched::thread::sleep_until(clock::get()->time() + convert(*req));
    return 0;
}
