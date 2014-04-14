/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Paweł Dziepak
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _TST_MMAP_HH
#define _TST_MMAP_HH

#include <sys/mman.h>
#include <signal.h>

#include <cassert>
#include <cstdint>
#include <cstdlib>

static void* align_page_down(void* x)
{
    uintptr_t addr = reinterpret_cast<uintptr_t>(x);

    return reinterpret_cast<void*>(addr & ~4095UL);
}

static bool segv_received = false;
static void segv_handler(int sig, siginfo_t* si, void* unused)
{
    assert(!segv_received);  // unexpected SIGSEGV loop
    segv_received = true;
    // Upon exit of this handler, the failing instruction will run again,
    // causing an endless loop of SIGSEGV. To avoid using longjmp to skip
    // the failing instruction, let's just make the relevant address
    // accessible, so the instruction will succeed. This is ugly, but
    // serves its purpose for this test...
    assert(mprotect(align_page_down(si->si_addr), 4096,
                    PROT_READ | PROT_WRITE | PROT_EXEC) == 0);
}

static void catch_segv()
{
    struct sigaction sa;

    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segv_handler;
    assert(sigaction(SIGSEGV, &sa, NULL) == 0);
}

static bool caught_segv()
{
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    assert(sigaction(SIGSEGV, &sa, NULL) == 0);
    bool ret = segv_received;
    segv_received = false;
    return ret;
}

// Try to write to the given address. NOTE: Currrently, on failure, this
// function uses mprotect() to change the protection of the given page...
// So don't use twice on the same page...
static inline bool try_write(void *addr)
{
    catch_segv();
    *(volatile char*)addr = 123;
    return !caught_segv();
}

static inline bool try_write(int (*func)())
{
    catch_segv();
    char byte = **(volatile char**)&func;
    **(volatile char**)&func = byte;
    return !caught_segv();
}

static inline bool try_read(void *addr)
{
    catch_segv();
    // The following "unused" and "volatile" crap are necessary to convince
    // the compiler, optimizer and Eclipse that we really want this useless
    // read :-)
    char __attribute__((unused)) z = *(volatile char*)addr;
    return !caught_segv();
}

static inline bool try_execute(int (*func)())
{
    catch_segv();
    int __attribute__((unused)) z = func();
    return !caught_segv();
}

#endif
