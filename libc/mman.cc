/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/mman.h>
#include <memory>
#include <osv/mmu.hh>
#include <osv/mempool.hh>
#include <osv/debug.hh>
#include "osv/trace.hh"
#include "osv/dentry.h"
#include "osv/mount.h"
#include <osv/stubbing.hh>
#include "libc/libc.hh"
#include <safe-ptr.hh>

#ifndef MAP_UNINITIALIZED
#define MAP_UNINITIALIZED 0x4000000
#endif

TRACEPOINT(trace_memory_mmap, "addr=%p, length=%d, prot=%d, flags=%d, fd=%d, offset=%d", void *, size_t, int, int, int, off_t);
TRACEPOINT(trace_memory_mmap_err, "%d", int);
TRACEPOINT(trace_memory_mmap_ret, "%p", void *);
TRACEPOINT(trace_memory_munmap, "addr=%p, length=%d", void *, size_t);
TRACEPOINT(trace_memory_munmap_err, "%d", int);
TRACEPOINT(trace_memory_munmap_ret, "");

// Needs to be here, because java.so won't end up composing the kernel
size_t jvm_heap_size = 0;
void *jvm_heap_region = nullptr;
void *jvm_heap_region_end = nullptr;

unsigned libc_flags_to_mmap(int flags)
{
    unsigned mmap_flags = 0;
    if (flags & MAP_FIXED) {
        mmap_flags |= mmu::mmap_fixed;
    }
    if (flags & MAP_POPULATE) {
        mmap_flags |= mmu::mmap_populate;
    }
    if (flags & MAP_STACK) {
        // OSv currently requires that stacks be pinned (see issue #143). So
        // if an application wants to mmap() a stack for pthread_attr_setstack
        // and did us the courtesy of telling this to ue (via MAP_STACK),
        // let's return the courtesy by returning pre-faulted memory.
        // FIXME: If issue #143 is fixed, this workaround should be removed.
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

unsigned libc_madvise_to_advise(int advice)
{
    if (advice == MADV_DONTNEED) {
        return mmu::advise_dontneed;
    } else if (advice == MADV_NOHUGEPAGE) {
        return mmu::advise_nohugepage;
    }
    return 0;
}

OSV_LIBC_API
int mprotect(void *addr, size_t len, int prot)
{
    // we don't support mprotecting() the linear map (e.g.., malloc() memory)
    // because that could leave the linear map a mess.
    if (reinterpret_cast<long>(addr) < 0) {
        abort("mprotect() on linear map not supported\n");
    }

    if (!mmu::is_page_aligned(addr)) {
        // address not page aligned
        return libc_error(EINVAL);
    }

    len = align_up(len, mmu::page_size);
    return mmu::mprotect(addr, len, libc_prot_to_perm(prot)).to_libc();
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

OSV_LIBC_API
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

#ifndef AARCH64_PORT_STUB
    if ((flags & MAP_32BIT) && !(flags & MAP_FIXED) && !addr) {
        // If addr is not specified, OSv by default starts mappings at address
        // 0x200000000000ul (see mmu::allocate()).  MAP_32BIT asks for a lower
        // default. If MAP_FIXED or addr were specified, the default does not
        // matter anyway.
        addr = (void*)0x2000000ul;
    }
#endif
    if (flags & MAP_ANONYMOUS) {
        // We have already determined (see below) the region where the heap must be located. Now the JVM will request
        // fixed mappings inside that region
        if (jvm_heap_size && (addr >= jvm_heap_region) && (addr + length <= jvm_heap_region_end) && (mmap_flags & mmu::mmap_fixed)) {
            // Aside from the heap areas, the JVM will also span a new area for
            // the card table, which has variable size but is always small,
            // around 20 something MB even for heap sizes as large as 8G. With
            // the current code, this area will also be marked with the JVM
            // heap flag, even though it shouldn't technically be. I will leave
            // it this way now because it is simpler and I don't expect that to
            // ever be harmful.
            mmap_flags |= mmu::mmap_jvm_heap;
            if (memory::balloon_api) {
                memory::balloon_api->return_heap(length);
            }
        }
        try {
            ret = mmu::map_anon(addr, length, mmap_flags, mmap_perm);
        } catch (error& err) {
            err.to_libc(); // sets errno
            trace_memory_mmap_err(errno);
            return MAP_FAILED;
        }
        // has a hint, is bigger than the heap size, and we don't request a fixed address. The heap will later on be here.
        if (addr && jvm_heap_size && (length >= jvm_heap_size) && !(mmap_flags & mmu::mmap_fixed)) {
            jvm_heap_region = ret;
            jvm_heap_region_end = ret + length;
        }
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

int munmap_validate(void *addr, size_t length)
{
    if (!mmu::is_page_aligned(addr) || length == 0) {
        return EINVAL;
    }
    return 0;
}

OSV_LIBC_API
int munmap(void *addr, size_t length)
{
    trace_memory_munmap(addr, length);
    int error = munmap_validate(addr, length);
    if (error) {
        errno = error;
        trace_memory_munmap_err(error);
        return -1;
    }
    int ret = mmu::munmap(addr, length).to_libc();
    if (ret == -1) {
        trace_memory_munmap_err(errno);
    }
    trace_memory_munmap_ret();
    return ret;
}

OSV_LIBC_API
int msync(void *addr, size_t length, int flags)
{
    return mmu::msync(addr, length, flags).to_libc();
}

OSV_LIBC_API
int mincore(void *addr, size_t length, unsigned char *vec)
{
    if (!mmu::is_page_aligned(addr)) {
        return libc_error(EINVAL);
    }

    return mmu::mincore(addr, length, vec).to_libc();
}

OSV_LIBC_API
int madvise(void *addr, size_t length, int advice)
{
    auto err = mmu::advise(addr, length, libc_madvise_to_advise(advice));
    return err.to_libc();
}

OSV_LIBC_API
int brk(void *addr)
{
    WARN_STUBBED();
    errno = ENOMEM;
    return -1;
}

OSV_LIBC_API
void *sbrk(intptr_t increment)
{
    WARN_STUBBED();
    errno = ENOMEM;
    return (void *)-1;
}
