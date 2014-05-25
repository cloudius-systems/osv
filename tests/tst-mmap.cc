/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// To compile this test on Linux, use:
// g++ -g -pthread -std=c++11 -Wno-pointer-arith tests/tst-mmap.cc

#include "tst-mmap.hh"

#ifdef __OSV__
#include <osv/sched.hh>
#endif

#include <sys/mman.h>
#include <string.h>

#include <iostream>
#include <cassert>
#include <cstdlib>

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
    munmap(buf, hugepagesize*10);

    // Test for missing MAP_PRIVATE or MAP_SHARED flag
    buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    assert(buf == MAP_FAILED);

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

    }, sched::thread::attr().pin(sched::cpus[0]));
    t2 = new sched::thread([&]{
        // wait for t1 to asks us to mprotect
        sched::thread::wait_until([&] { return state.load() == 1; });
        mprotect(buf, 4096, PROT_READ);
        state.store(2);
        t1->wake();
    }, sched::thread::attr().pin(sched::cpus[1]));
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
    munmap(buf, 4096);

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
    unsigned char non_resident[10];
    memset(non_resident, 0x00, 10);
    assert(memcmp(vec, non_resident, 10) == 0);
    unsigned char partially_resident[10] = { 0x01, 0x00, 0x01, 0x00 };
    *(char*)buf = 123;
    *(char*)(buf + 4096*2) = 123;
    assert(mincore(buf, 4096*10, vec) == 0);
    assert(memcmp(vec, partially_resident, 10) == 0);
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

    // MAP_POPULATE and mincore()
    buf = mmap(NULL, 4096*10, PROT_READ|PROT_WRITE, MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf != MAP_FAILED);
    assert(mincore(buf, 4096*10, vec) == 0);
    unsigned char resident[10];
    memset(resident, 0x01, 10);
    assert(memcmp(vec, resident, 10) == 0);
    munmap(buf, 4096*10);

    // While msync() only works on mmapped memory, mincore() should also
    // succeed on non-mmapped memory, such as stack variables and malloc().
    char x;
    assert(mincore(align_page_down(&x), 1, vec) == 0);
    assert(mincore(align_page_down(&x)+1, 1, vec) == -1);
    void *y = malloc(1);
    assert(mincore(align_page_down(y), 1, vec) == 0);
    assert(mincore(align_page_down(y)+1, 1, vec) == -1);
    free(y);

    // If we are able to map a smaller-than-a-page region (that will be
    // obviously aligned internally), we should be able to unmap using the same
    // size.
    void *small = mmap(NULL, 64, PROT_READ, MAP_ANON | MAP_PRIVATE, -1, 0);
    assert(small != nullptr);
    assert(munmap(small, 64) == 0);

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
