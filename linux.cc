// linux syscalls

#include "debug.hh"
#include <boost/format.hpp>
#include "sched.hh"

#include <syscall.h>

long gettid()
{
    return sched::thread::current()->id();
}

long syscall(long number, ...)
{
    switch (number) {
    case __NR_gettid: return gettid();
    }

    debug(fmt("unimplemented syscall %d\n") % number);
    abort();
}
