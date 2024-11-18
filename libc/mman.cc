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
#include <atomic>
#include <osv/kernel_config_memory_jvm_balloon.h>

#ifndef MAP_UNINITIALIZED
#define MAP_UNINITIALIZED 0x4000000
#endif

TRACEPOINT(trace_memory_mmap, "addr=%p, length=%d, prot=%d, flags=%d, fd=%d, offset=%d", void *, size_t, int, int, int, off_t);
TRACEPOINT(trace_memory_mmap_err, "%d", int);
TRACEPOINT(trace_memory_mmap_ret, "%p", void *);
TRACEPOINT(trace_memory_munmap, "addr=%p, length=%d", void *, size_t);
TRACEPOINT(trace_memory_munmap_err, "%d", int);
TRACEPOINT(trace_memory_munmap_ret, "");

#if CONF_memory_jvm_balloon
// Needs to be here, because java.so won't end up composing the kernel
size_t jvm_heap_size = 0;
void *jvm_heap_region = nullptr;
void *jvm_heap_region_end = nullptr;
#endif

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
        mmap_flags |= mmu::mmap_stack;
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
#if CONF_memory_jvm_balloon
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
#endif
        try {
            ret = mmu::map_anon(addr, length, mmap_flags, mmap_perm);
        } catch (error& err) {
            err.to_libc(); // sets errno
            trace_memory_mmap_err(errno);
            return MAP_FAILED;
        }
#if CONF_memory_jvm_balloon
        // has a hint, is bigger than the heap size, and we don't request a fixed address. The heap will later on be here.
        if (addr && jvm_heap_size && (length >= jvm_heap_size) && !(mmap_flags & mmu::mmap_fixed)) {
            jvm_heap_region = ret;
            jvm_heap_region_end = ret + length;
        }
#endif
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

// The brk/sbrk/program break implementation is quite simple
// and is based on mmap().
// In essence, on the very 1st call to brk() or sbr(), we call
// initialize_program_break() to initialize anonymous unpopulated
// private virtual memory mapping. The mapping size is roughly
// equal to the amount of free physical memory available at this
// point rounded down to the nearest huge page. This should be
// more than enough to satisfy growth of the program break upward.
// Given the mapping is unpopulated, we do not really use any physical
// memory until the program break moves up and access to corresponding
// pages triggers a fault. We also try to give the physical memory back
// to the system in rare cases when program break move back down.
//
// Given the mapping is rounded to the whole huge page, the underlying
// physical memory will grow in 2M chunks which seems like a good compromise
// given program break will not be used for large memory allocations.
// In future we may decide to make the mapping mapped in small 4K pages
// and grow accordingly to better save physical memory. This would however
// be more costly at page mapping tables level.
//
// Please note that neither the program break nor brk/sbrk are thread-safe
// by design and the use of it needs to use proper locking around it.
static void *initial_program_break = NULL;
// We use atomic to make sure the program break changes are visible consistently
// across all CPUs regardless of the weak or strong memory model
static std::atomic<void*> program_break = {NULL};
static size_t break_area_size = 0;

static bool initialize_program_break()
{
    if (!program_break) {
        break_area_size = align_down(memory::stats::free(), mmu::huge_page_size);
        program_break = initial_program_break = mmap(NULL, break_area_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
        return initial_program_break != MAP_FAILED;
    } else {
        return true;
    }
}

void *get_program_break()
{
    return program_break.load();
}

static int internal_brk(void *addr)
{
    if (addr) {
        // Check if new program break falls into a mapped area of memory
        if (addr >= initial_program_break && addr < initial_program_break + break_area_size) {
            if (addr < program_break.load()) {
                // The rare case when the program break goes down. In this case
                // let us identify potential whole huge pages of the mapping we
                // can depopulate and return physical memory to the system
                void *depopulate_start = align_up(addr, mmu::huge_page_size);
                void *depopulate_end = align_up(program_break.load(), mmu::huge_page_size);
                if (depopulate_start < depopulate_end) {
                    size_t depopulate_size = reinterpret_cast<uintptr_t>(depopulate_end) - reinterpret_cast<uintptr_t>(depopulate_start);
                    mmu::advise(depopulate_start, depopulate_size, mmu::advise_dontneed);
                }
            } else {
                // The program break moves up. In this case let us identify the new memory area
                // and initialize it to zero per the specification
                size_t new_memory_area = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(program_break.load());
                memset(program_break.load(), 0, new_memory_area);
            }
            program_break = addr;
            return 0;
        } else {
            // Invalid program break address
            errno = ENOMEM;
            return -1;
        }
    } else {
        return 0;
    }
}

OSV_LIBC_API
int brk(void *addr)
{
    if (!initialize_program_break()) {
        errno = ENOMEM;
        return -1;
    }
    return internal_brk(addr);
}

OSV_LIBC_API
void *sbrk(intptr_t increment)
{
    if (!initialize_program_break()) {
        errno = ENOMEM;
        return (void *)-1;
    }
    if (!increment) {
        // If 0 return current program break
        return program_break.load();
    } else {
        // Otherwise increment or decrement the break by
        // delegating to internal_brk()
        auto old_break = program_break.load();
        if (!internal_brk(old_break + increment)) {
            return old_break;
        } else {
            return (void *)-1;
        }
    }
}

static unsigned posix_madvise_to_advise(int advice)
{
    if (advice == POSIX_MADV_DONTNEED) {
        return mmu::advise_dontneed;
    }
    return 0;
}

OSV_LIBC_API
int posix_madvise(void *addr, size_t len, int advice) {
    auto err = mmu::advise(addr, len, posix_madvise_to_advise(advice));
    return err.get();
}
