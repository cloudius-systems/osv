// linux syscalls

#include "drivers/console.hh"
#include <boost/format.hpp>

#define _GNU_SOURCE
#include <unistd.h>
#include <linux/unistd.h>

extern Console* debug_console;

typedef boost::format fmt;

long syscall(long number, ...)
{
    debug_console->writeln(fmt("unimplemented syscall %d") % number);
    abort();
}
