#include <sys/time.h>
#include <time.h>
#include "libc.hh"
#include "drivers/clock.hh"
#include "sched.hh"

u64 convert(const timespec& ts)
{
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

extern "C"
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

int clock_gettime(clockid_t clk_id, struct timespec* ts)
{
    if (clk_id != CLOCK_REALTIME) {
        return libc_error(EINVAL);
    }
    u64 time = clock::get()->time();
    auto sec = time / 1000000000;
    auto nsec = time % 1000000000;
    ts->tv_sec = sec;
    ts->tv_nsec = nsec;
    return 0;
}

extern "C"
int __clock_gettime(clockid_t clk_id, struct timespec* ts) __attribute__((alias("clock_gettime")));

int clock_getres(clockid_t clk_id, struct timespec* ts)
{
    if (clk_id != CLOCK_REALTIME) {
        return libc_error(EINVAL);
    }

    if (ts) {
        ts->tv_sec = 0;
        ts->tv_nsec = 1;
    }

    return 0;
}

int clock_getcpuclockid(pid_t pid, clockid_t* clock_id)
{
    return libc_error(ENOSYS);
}
