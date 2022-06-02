//#include "libc.h"
#include <time.h>
#include <sys/time.h>

__attribute__((__visibility__("default")))
time_t __vdso_time(time_t *tloc)
{
    return time(tloc);
}

__attribute__((__visibility__("default")))
int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    return gettimeofday(tv, tz);
}

__attribute__((__visibility__("default")))
int __vdso_clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    return clock_gettime(clk_id, tp);
}
