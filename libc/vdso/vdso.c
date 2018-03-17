//#include "libc.h"
#include <time.h>
#include <sys/time.h>

time_t __vdso_time(time_t *tloc)
{
    return time(tloc);
}

int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    return gettimeofday(tv, tz);
}

int __vdso_clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    return clock_gettime(clk_id, tp);
}
