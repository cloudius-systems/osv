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
#include "osv/dentry.h"
#include "osv/mount.h"
#include "libc/libc.hh"
#include <safe-ptr.hh>

TRACEPOINT(trace_memory_mmap, "addr=%p, length=%d, prot=%d, flags=%d, fd=%d, offset=%d", void *, size_t, int, int, int, off_t);
TRACEPOINT(trace_memory_mmap_err, "%d", int);
TRACEPOINT(trace_memory_mmap_ret, "%p", void *);
TRACEPOINT(trace_memory_munmap, "addr=%p, length=%d", void *, size_t);
TRACEPOINT(trace_memory_munmap_err, "%d", int);
TRACEPOINT(trace_memory_munmap_ret, "");

unsigned libc_flags_to_mmap(int flags)
{
    unsigned mmap_flags = 0;
    if (flags & MAP_FIXED) {
        mmap_flags |= mmu::mmap_fixed;
    }
    if (flags & MAP_POPULATE) {
        mmap_flags |= mmu::mmap_populate;
    }
    if (flags & MAP_SHARED) {
        mmap_flags |= mmu::mmap_shared;
    }
    if (flags & MAP_UNINITIALIZED) {
        mmap_flags |= mmu::mmap_uninitialized;
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

    if (!mmu::is_page_aligned(addr) || !mmu::is_page_aligned(len)) {
        // address not page aligned
        return libc_error(EINVAL);
    }
    if (!mmu::ismapped(addr, len)) {
        return libc_error(ENOMEM);
    }

    return mmu::protect(addr, len, libc_prot_to_perm(prot)).to_libc();
}

int mmap_validate(void *addr, size_t length, int flags, off_t offset)
{
    int type = flags & (MAP_SHARED|MAP_PRIVATE);
    // Either MAP_SHARED or MAP_PRIVATE must be set, but not both.
    if (!type || type == (MAP_SHARED|MAP_PRIVATE)) {
        return EINVAL;
    }
    if ((flags & MAP_FIXED && !mmu::is_page_aligned(addr)) ||
        !mmu::is_page_aligned(offset) || length == 0) {
        return EINVAL;
    }
    return 0;
}

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset)
{
    trace_memory_mmap(addr, length, prot, flags, fd, offset);

    int err = mmap_validate(addr, length, flags, offset);
    if (err) {
        errno = err;
        trace_memory_mmap_err(err);
        return MAP_FAILED;
    }

    // make use the payload isn't remapping physical memory
    assert(reinterpret_cast<long>(addr) >= 0);

    void *ret;

    auto mmap_flags = libc_flags_to_mmap(flags);
    auto mmap_perm  = libc_prot_to_perm(prot);

    if (flags & MAP_ANONYMOUS) {
        ret = mmu::map_anon(addr, length, mmap_flags, mmap_perm);
    } else {
        fileref f(fileref_from_fd(fd));
        if (!f) {
            errno = EBADF;
            trace_memory_mmap_err(errno);
            return MAP_FAILED;
        }
        try {
            ret = mmu::map_file(addr, length, mmap_flags, mmap_perm, f, offset);
        } catch (error& err) {
            err.to_libc(); // sets errno
            trace_memory_mmap_err(errno);
            return MAP_FAILED;
        }
    }
    trace_memory_mmap_ret(ret);
    return ret;
}

extern "C" void *mmap64(void *addr, size_t length, int prot, int flags,
                      int fd, off64_t offset)
    __attribute__((alias("mmap")));


int munmap_validate(void *addr, size_t length)
{
    if (!mmu::is_page_aligned(addr) || length == 0) {
        return EINVAL;
    }
    if (!mmu::ismapped(addr, length)) {
        return EINVAL;
    }
    return 0;
}

int munmap(void *addr, size_t length)
{
    trace_memory_munmap(addr, length);
    int error = munmap_validate(addr, length);
    if (error) {
        errno = error;
        trace_memory_munmap_err(error);
        return -1;
    }
    mmu::msync(addr, length, 0);
    mmu::unmap(addr, length);
    trace_memory_munmap_ret();
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

int mincore(void *addr, size_t length, unsigned char *vec)
{
    if (!mmu::is_page_aligned(addr)) {
        return libc_error(EINVAL);
    }
    if (!mmu::is_linear_mapped(addr, length) && !mmu::ismapped(addr, length)) {
        return libc_error(ENOMEM);
    }
    char *end = align_up((char *)addr + length, mmu::page_size);
    char tmp;
    for (char *p = (char *)addr; p < end; p += mmu::page_size) {
        if (safe_load(p, tmp)) {
            *vec++ = 0x01;
        } else {
            *vec++ = 0x00;
        }
    }
    return 0;
}
