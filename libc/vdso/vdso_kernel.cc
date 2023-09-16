#include <time.h>
#include <sys/time.h>
#include "tls-switch.hh"

extern "C" __attribute__((__visibility__("default")))
time_t __vdso_kernel_time(time_t *tloc)
{
    arch::tls_switch_on_app_stack _tls_switch;
    return time(tloc);
}

extern "C" __attribute__((__visibility__("default")))
int __vdso_kernel_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    arch::tls_switch_on_app_stack _tls_switch;
    return gettimeofday(tv, tz);
}

extern "C" __attribute__((__visibility__("default")))
int __vdso_kernel_clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    arch::tls_switch_on_app_stack _tls_switch;
    return clock_gettime(clk_id, tp);
}
