#include <sys/mman.h>
#include <memory>
#include "mmu.hh"
#include "debug.hh"

unsigned libc_prot_to_perm(int prot)
{
    unsigned perm = 0;
    if (prot & PROT_READ) {
        perm |= mmu::perm_read;
    }
    if (prot & PROT_WRITE) {
        perm |= mmu::perm_write;
    }
    if (prot & PROT_EXEC) {
        perm |= mmu::perm_exec;
    }
    return perm;
}

int mprotect(void *addr, size_t len, int prot)
{
    debug("stub mprotect()");
    return 0;
}

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset)
{
    // make use the payload isn't remapping physical memory
    assert(reinterpret_cast<long>(addr) >= 0);
    std::unique_ptr<mmu::vma> v;
    if (!(flags & MAP_FIXED)) {
        v.reset(mmu::reserve(addr, length));
        addr = v->addr();
    }
    if (fd == -1) {
        mmu::map_anon(addr, length, libc_prot_to_perm(prot));
    } else {
        file f(fd);
        mmu::map_file(addr, length, libc_prot_to_perm(prot), f, offset);
    }
    v.release();
    return addr;
}

extern "C" void *mmap64(void *addr, size_t length, int prot, int flags,
                      int fd, off64_t offset)
    __attribute__((alias("mmap")));


int munmap(void *addr, size_t length)
{
    mmu::unmap(addr, length);
    return 0;
}
