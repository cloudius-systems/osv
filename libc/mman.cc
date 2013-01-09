#include <sys/mman.h>
#include <memory>
#include "mmu.hh"
#include "debug.hh"

int mprotect(void *addr, size_t len, int prot)
{
    debug("stub mprotect()");
    return 0;
}

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset)
{
    if (fd != -1) {
        abort();
    }

    std::unique_ptr<mmu::vma> v;
    if (!(flags & MAP_FIXED)) {
        v.reset(mmu::reserve(addr, length));
        addr = v->addr();
    }
    mmu::map_anon(addr, length, 0);
    v.release();
    return addr;
}

int munmap(void *addr, size_t length)
{
    mmu::unmap(addr, length);
    return 0;
}
