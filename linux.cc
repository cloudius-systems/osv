// linux syscalls

#include "debug.hh"
#include <boost/format.hpp>

#include <unistd.h>
#include <linux/unistd.h>

long gettid()
{
    return 0; // FIXME
}

long syscall(long number, ...)
{
    switch (number) {
    case __NR_gettid: return gettid();
    }

    debug(fmt("unimplemented syscall %d\n") % number);
    abort();
}
