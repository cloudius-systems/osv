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
    // we don't support mprotecting() the linear map (e.g.., malloc() memory)
    // because that could leave the linear map a mess.
    if (reinterpret_cast<long>(addr) < 0) {
        debug("mprotect() on linear map not supported\n");
        abort();
    }

    if ((reinterpret_cast<intptr_t>(addr) & 4095) || (len & 4095)) {
        // address not page aligned
        errno = EINVAL;
        return -1;
    }
    if (!mmu::protect(addr, len, libc_prot_to_perm(prot))) {
        // NOTE: we return ENOMEM when part of the range was not mapped,
        // but nevertheless, set the protection on the rest!
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset)
{
    // TODO: should fail with EINVAL in some cases of addr, length, offset.

    // make use the payload isn't remapping physical memory
    assert(reinterpret_cast<long>(addr) >= 0);

    if (fd == -1) {
        return mmu::map_anon(addr, length, !(flags & MAP_FIXED),
                libc_prot_to_perm(prot));
    } else {
        fileref f(fileref_from_fd(fd));
        return mmu::map_file(addr, length, !(flags & MAP_FIXED),
                libc_prot_to_perm(prot), f, offset);
    }
}

extern "C" void *mmap64(void *addr, size_t length, int prot, int flags,
                      int fd, off64_t offset)
    __attribute__((alias("mmap")));


int munmap(void *addr, size_t length)
{
    // TODO: fail with EINVAL in some cases of addr, length.
    mmu::unmap(addr, length);
    return 0;
}

int msync(void *addr, size_t length, int flags)
{
    if (!mmu::ismapped(addr, length)) {
        errno = ENOMEM;
        return -1;
    }
    // FIXME: The implementation is missing. We didn't do any synching -
    // just check if the given memory region is mapped... libunwind, which
    // we use for backtrace(), uses msync() for just check checking.
    return 0;
}
