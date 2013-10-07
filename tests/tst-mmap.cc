/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifdef __OSV__
#include "sched.hh"
#endif

#include <sys/mman.h>
#include <signal.h>

#include <iostream>
#include <cassert>
#include <cstdlib>

static bool segv_received = false;
static void segv_handler(int sig, siginfo_t *si, void *unused)
{
    assert(!segv_received);  // unexpected SIGSEGV loop
    segv_received = true;
    // Upon exit of this handler, the failing instruction will run again,
    // causing an endless loop of SIGSEGV. To avoid using longjmp to skip
    // the failing instruction, let's just make the relevant address
    // accessible, so the instruction will succeed. This is ugly, but
    // serves its purpose for this test...
    assert(mprotect(si->si_addr, 4096, PROT_READ|PROT_WRITE|PROT_EXEC)==0);
}
static void catch_segv()
{
    struct sigaction sa;

    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segv_handler;
    assert(sigaction(SIGSEGV, &sa, NULL)==0);
}
static bool caught_segv()
{
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    assert(sigaction(SIGSEGV, &sa, NULL)==0);
    bool ret = segv_received;
    segv_received = false;
    return ret;
}

// Try to write to the given address. NOTE: Currrently, addr must be page
// aligned, and on failure, this function uses mprotect() to change the
// protection of the given page... So don't use twice on the same page...
static bool try_write(void *addr)
{
    catch_segv();
    *(char*)addr = 123;
    return (!caught_segv());
}

static bool try_read(void *addr)
{
    catch_segv();
    // The following "unused" and "volatile" crap are necessary to convince
    // the compiler, optimizer and Eclipse that we really want this useless
    // read :-)
    char __attribute__((unused)) z = *(volatile char*)addr;
    return (!caught_segv());
}

static void* align_page_down(void *x)
{
    uintptr_t addr = reinterpret_cast<uintptr_t>(x);

    return reinterpret_cast<void*>(addr & ~4095UL);
}

int main(int argc, char **argv)
{
    std::cerr << "Running mmap tests\n";
    // Test that munmap actually recycles the physical memory allocated by mmap
    for (int i = 0; i < 1000; i++) {
        constexpr size_t size = 1<<20;
        void *buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
        assert(buf != MAP_FAILED);
        munmap(buf, size);
    }
    // Do the same for allocations large enough to use huge-pages
    for (int i = 0; i < 100; i++) {
        constexpr size_t size = 30 * 1<<20;
        void *buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
        assert(buf != MAP_FAILED);
        munmap(buf, size);
    }
    // Test that we can override mmaps, without munmap, without leaking
    // physical memory. Mix in small page and huge page allocations for
    // more fun.
    int hugepagesize = 1<<21;
    void *buf = mmap(NULL, hugepagesize*10, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    for (int i=0; i<100; i++) {
        mmap(buf, hugepagesize-4096, PROT_READ, MAP_ANONYMOUS|MAP_FIXED, -1, 0);
        assert(buf != MAP_FAILED);
        mmap(buf, hugepagesize*9+4096, PROT_READ, MAP_ANONYMOUS|MAP_FIXED, -1, 0);
        assert(buf != MAP_FAILED);
    }
    munmap(buf, hugepagesize*9+4096);

    // test mprotect making a read-only page.
    buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    mprotect(buf, 4096, PROT_READ);
    assert(!try_write(buf));
    munmap(buf, 4096);

    // test mprotect again, with part of huge page, and see that it only
    // modifies the desired part and not anything else
    buf = mmap(NULL, 3*hugepagesize, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    void *hp = (void*) (((uintptr_t)buf&~(hugepagesize-1))+hugepagesize);
    mprotect(hp+4096, 4096, PROT_READ);
    assert(try_write(hp));
    assert(!try_write(hp+4096));
    assert(try_write(hp+8192));
    munmap(buf, 3*hugepagesize);

    // test that mprotect with PROT_NONE disables even read
    buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    mprotect(buf, 4096, PROT_NONE);
    assert(!try_read(buf));
    munmap(buf, 4096);

    // Tests similar to the above, but giving reduced permissions on
    // mmap() itself instead of calling mprotect
    buf = mmap(NULL, 4096, PROT_READ, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    assert(!try_write(buf));
    munmap(buf, 4096);

    buf = mmap(NULL, 4096, PROT_NONE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    assert(!try_read(buf));
    munmap(buf, 4096);

    buf = mmap(NULL, 4096, PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    assert(try_write(buf));
    munmap(buf, 4096);


    // Try successfully writing to an address, and immediately trying to
    // forbid it. If we don't TLB flush correctly, it might erroneously
    // succeed!
    buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    *(char*)buf = 0; // write will succeed
    mprotect(buf, 4096, PROT_READ);
    assert(!try_write(buf));
    munmap(buf, 4096);

#ifdef __OSV__
    // Try the same, but in two separate processors: Processor 0 successfully
    // writes to an address, processor 1 protects it, and then processor 0
    // tries to write again (and should fail). For this test to succeed, we
    // must correctly flush the TLB of all processors, not just the one
    // running mprotect.
    assert(sched::cpus.size() >= 2); // this test can't work without 2 cpus...
    std::atomic_int state(0);
    buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    sched::thread *t2 = nullptr;
    sched::thread *t1 = new sched::thread([&]{
        *(char*)buf = 0; // write will succeed
        // wait for the t2 object to exist (not necessarily run)
        sched::thread::wait_until([&] { return t2 != nullptr; });
        // tell t2 to mprotect the buffer
        state.store(1);
        t2->wake();
        // wait for t2 to mprotect
        sched::thread::wait_until([&] { return state.load() == 2; });
        // and see that it properly TLB flushed also t1's CPU
        assert(!try_write(buf));

    }, sched::thread::attr(sched::cpus[0]));
    t2 = new sched::thread([&]{
        // wait for t1 to asks us to mprotect
        sched::thread::wait_until([&] { return state.load() == 1; });
        mprotect(buf, 4096, PROT_READ);
        state.store(2);
        t1->wake();
    }, sched::thread::attr(sched::cpus[1]));
    t1->start();
    t2->start();
    delete t1; // also join()s the thread
    delete t2;
    munmap(buf, 4096);
#endif

    // Test that mprotect() only hides memory, doesn't free it
    buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    *(char*)buf = 123;
    mprotect(buf, 4096, PROT_NONE); // hide the memory - but don't unmap
    mprotect(buf, 4096, PROT_READ); // get it back
    assert(*(char*)buf == 123);

//        // Test that mprotect() on malloc() memory is currently not supported
//        buf = malloc(8192);
//        buf = align_up(buf, 4096);
//        assert(mprotect(buf, 4096, PROT_READ)==-1);
//        assert(errno==ENOTSUP);
//        free(buf);

    // Test that msync() can tell if a range of memory is mapped.
    // libunwind's functions which we use for backtrace() require this.
    buf = mmap(NULL, 4096*10, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    assert(msync(buf, 4096*10, MS_ASYNC) == 0);
    assert(msync(buf, 4096*9, MS_ASYNC) == 0);
    assert(msync(buf+4096, 4096*9, MS_ASYNC) == 0);
    munmap(buf+4096, 4096*3);
    munmap(buf+4096*7, 4096*2);
    assert(msync(buf, 4096*10, MS_ASYNC) == -1);
    assert(msync(buf, 4096, MS_ASYNC) == 0);
    assert(msync(buf+4096, 4096, MS_ASYNC) == -1);
    assert(msync(buf+4096*4, 3*4096, MS_ASYNC) == 0);
    assert(msync(buf+4096*5, 1*4096, MS_ASYNC) == 0);
    assert(msync(buf+4096, 4096, MS_ASYNC) == -1);
    assert(msync(buf, 3*4096, MS_ASYNC) == -1);
    assert(msync(buf+3*4096, 3*4096, MS_ASYNC) == -1);
    munmap(buf, 4096*10);

    // Similarly test mincore().
    unsigned char vec[20];
    buf = mmap(NULL, 4096*10, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    assert(mincore(buf, 4096*10, vec) == 0);
    assert(mincore(buf, 4096*9, vec) == 0);
    assert(mincore(buf+4096, 4096*9, vec) == 0);
    munmap(buf+4096, 4096*3);
    munmap(buf+4096*7, 4096*2);
    assert(mincore(buf, 4096*10, vec) == -1);
    assert(mincore(buf, 4096, vec) == 0);
    assert(mincore(buf+4096, 4096, vec) == -1);
    assert(mincore(buf+4096*4, 3*4096, vec) == 0);
    assert(mincore(buf+4096*5, 1*4096, vec) == 0);
    assert(mincore(buf+4096, 4096, vec) == -1);
    assert(mincore(buf, 3*4096, vec) == -1);
    assert(mincore(buf+3*4096, 3*4096, vec) == -1);
    munmap(buf, 4096*10);

    // While msync() only works on mmapped memory, mincore() should also
    // succeed on non-mmapped memory, such as stack variables and malloc().
    char x;
    assert(mincore(align_page_down(&x), 1, vec) == 0);
    void *y = malloc(1);
    assert(mincore(align_page_down(y), 1, vec) == 0);
    free(y);

    // TODO: verify that mmapping more than available physical memory doesn't
    // panic just return -1 and ENOMEM.
    // TODO: verify that huge-page-sized allocations get a huge-page aligned address
    // (if addr=0). Not critical, though, just makes sense.
    // TODO: verify that various calls to mmap() and munmap() (length=0, unaligned
    // address, etc.) fail with EINVAL.
    // TODO: test that mprotect() over malloc()ed memory (not just mmap()) works.
    std::cerr << "mmap tests succeeded\n";
    return 0;
}
