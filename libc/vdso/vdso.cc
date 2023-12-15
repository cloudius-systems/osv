#include <time.h>
#include <sys/time.h>

#ifdef __x86_64__
#include "tls-switch.hh"
extern "C" __attribute__((__visibility__("default")))
time_t __vdso_time(time_t *tloc)
{
    arch::tls_switch _tls_switch;
    return time(tloc);
}

extern "C" __attribute__((__visibility__("default")))
int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    arch::tls_switch _tls_switch;
    return gettimeofday(tv, tz);
}

extern "C" __attribute__((__visibility__("default")))
int __vdso_clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    arch::tls_switch _tls_switch;
    if (clock_gettime(clk_id, tp) < 0) {
        return -errno;
    } else {
        return 0;
    }
}
#endif

#ifdef __aarch64__
__attribute__((__visibility__("default")))
int __kernel_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    return gettimeofday(tv, tz);
}

__attribute__((__visibility__("default")))
int __kernel_clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    if (clock_gettime(clk_id, tp) < 0) {
        return -errno;
    } else {
        return 0;
    }
}

__attribute__((__visibility__("default")))
int __kernel_clock_getres(clockid_t clk_id, struct timespec *tp)
{
    return clock_getres(clk_id, tp);
}
#endif
