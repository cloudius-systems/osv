// linux syscalls

#include "drivers/console.hh"
#include <boost/format.hpp>

#define _GNU_SOURCE
#include <unistd.h>
#include <linux/unistd.h>

extern Console* debug_console;

typedef boost::format fmt;

long gettid()
{
    return 0; // FIXME
}

long syscall(long number, ...)
{
    switch (number) {
    case __NR_gettid: return gettid();
    }

    debug_console->writeln(fmt("unimplemented syscall %d") % number);
    abort();
}
