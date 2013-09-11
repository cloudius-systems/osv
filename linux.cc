// linux syscalls

#include "debug.hh"
#include <boost/format.hpp>
#include "sched.hh"

#include <syscall.h>
#include <stdarg.h>
#include <time.h>

long gettid()
{
    return sched::thread::current()->id();
}

long syscall(long number, ...)
{
    switch (number) {
    case __NR_gettid: return gettid();
    case __NR_clock_gettime: {
        va_list args;
        clockid_t arg1;
        struct timespec *arg2;
        va_start(args, number);
        arg1 = va_arg(args, typeof(arg1));
        arg2 = va_arg(args, typeof(arg2));
        va_end(args);
        return clock_gettime(arg1, arg2);
        }
    }

    debug("syscall(): unimplemented system call %d\n", number);
    abort();
}
