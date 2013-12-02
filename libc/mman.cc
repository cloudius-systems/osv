/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/mman.h>
#include <memory>
#include "mmu.hh"
#include "debug.hh"
#include "osv/trace.hh"
#include "libc/libc.hh"

TRACEPOINT(trace_memory_mmap, "addr=%p, length=%d, prot=%d, flags=%d, fd=%d, offset=%d", void *, size_t, int, int, int, off_t);
TRACEPOINT(trace_memory_mmap_err, "%d", int);
TRACEPOINT(trace_memory_mmap_ret, "%p", void *);
TRACEPOINT(trace_memory_munmap, "addr=%p, length=%d", void *, size_t);

unsigned libc_flags_to_mmap(int flags)
{
    unsigned mmap_flags = 0;
    if (flags & MAP_FIXED) {
        mmap_flags |= mmu::mmap_fixed;
    }
    if (flags & MAP_POPULATE) {
        mmap_flags |= mmu::mmap_populate;
    }
    return mmap_flags;
}

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
        return libc_error(EINVAL);
    }
    if (!mmu::ismapped(addr, len)) {
        return libc_error(ENOMEM);
    }
    if (!mmu::protect(addr, len, libc_prot_to_perm(prot))) {
        // NOTE: we return ENOMEM when part of the range was not mapped,
        // but nevertheless, set the protection on the rest!
        return libc_error(ENOMEM);
    }
    return 0;
}

int mmap_validate_flags(int flags)
{
    int type = flags & (MAP_SHARED|MAP_PRIVATE);
    // Either MAP_SHARED or MAP_PRIVATE must be set, but not both.
    if (!type || type == (MAP_SHARED|MAP_PRIVATE)) {
        return EINVAL;
    }
    return 0;
}

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset)
{
    trace_memory_mmap(addr, length, prot, flags, fd, offset);

    errno = mmap_validate_flags(flags);
    if (errno) {
        trace_memory_mmap_err(errno);
        return MAP_FAILED;
    }

    // TODO: should fail with EINVAL in some cases of addr, length, offset.

    // make use the payload isn't remapping physical memory
    assert(reinterpret_cast<long>(addr) >= 0);

    void *ret;
    if (fd == -1) {
        ret = mmu::map_anon(addr, length, libc_flags_to_mmap(flags),
                libc_prot_to_perm(prot));
    } else {
        fileref f(fileref_from_fd(fd));
        ret = mmu::map_file(addr, length, !(flags & MAP_FIXED),
                libc_prot_to_perm(prot), f, offset, flags & MAP_SHARED);
    }
    trace_memory_mmap_ret(ret);
    return ret;
}

extern "C" void *mmap64(void *addr, size_t length, int prot, int flags,
                      int fd, off64_t offset)
    __attribute__((alias("mmap")));


int munmap(void *addr, size_t length)
{
    trace_memory_munmap(addr, length);
    mmu::msync(addr, length, 0);
    // TODO: fail with EINVAL in some cases of addr, length.
    mmu::unmap(addr, length);
    return 0;
}

int msync(void *addr, size_t length, int flags)
{
    auto err = make_error(ENOMEM);
    if (mmu::ismapped(addr, length)) {
        err = mmu::msync(addr, length, flags);
    }
    return err.to_libc();
}
