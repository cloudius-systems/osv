#include <sys/mman.h>
#include "debug.hh"

int mprotect(void *addr, size_t len, int prot)
{
    debug("stub mprotect()");
    return 0;
}
