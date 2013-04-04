#include "tst-hub.hh"
#include "sched.hh"
#include "debug.hh"
#include "align.hh"

#include <sys/mman.h>
#include <signal.h>

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


class test_mmap: public unit_tests::vtest {

public:
    void run()
    {
        debug("Running mmap tests\n", false);
        // Test that munmap actually recycles the physical memory allocated by mmap
        for (int i=0; i<1000; i++) {
            constexpr size_t size = 1<<20;
            void *buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
            if(!buf)
                debug("mmap failed!\n",false);
            munmap(buf, size);
        }
        // Do the same for allocations large enough to use huge-pages
        for (int i=0; i<100; i++) {
            constexpr size_t size = 30 * 1<<20;
            void *buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
            if(!buf)
                debug("mmap failed!\n",false);
            munmap(buf, size);
        }
        // Test that we can override mmaps, without munmap, without leaking
        // physical memory. Mix in small page and huge page allocations for
        // more fun.
        int hugepagesize = 1<<21;
        void *buf = mmap(NULL, hugepagesize*10, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
        assert(buf);
        for (int i=0; i<100; i++) {
            mmap(buf, hugepagesize-4096, PROT_READ, MAP_ANONYMOUS|MAP_FIXED, -1, 0);
            mmap(buf, hugepagesize*9+4096, PROT_READ, MAP_ANONYMOUS|MAP_FIXED, -1, 0);
        }
        munmap(buf, hugepagesize*9+4096);

        // test mprotect making a read-only page.
        buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
        mprotect(buf, 4096, PROT_READ);
        assert(!try_write(buf));
        munmap(buf, 4096);

        // test mprotect again, with part of huge page, and see that it only
        // modifies the desired part and not anything else
        buf = mmap(NULL, 3*hugepagesize, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
        void *hp = (void*) (((uintptr_t)buf&~(hugepagesize-1))+hugepagesize);
        mprotect(hp+4096, 4096, PROT_READ);
        assert(try_write(hp));
        assert(!try_write(hp+4096));
        assert(try_write(hp+8192));
        munmap(buf, 3*hugepagesize);

        // test that mprotect with PROT_NONE disables even read
        buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
        mprotect(buf, 4096, PROT_NONE);
        assert(!try_read(buf));
        munmap(buf, 4096);

        // Tests similar to the above, but giving reduced permissions on
        // mmap() itself instead of calling mprotect
        buf = mmap(NULL, 4096, PROT_READ, MAP_ANONYMOUS, -1, 0);
        assert(!try_write(buf));
        munmap(buf, 4096);

        buf = mmap(NULL, 4096, PROT_NONE, MAP_ANONYMOUS, -1, 0);
        assert(!try_read(buf));
        munmap(buf, 4096);

        buf = mmap(NULL, 4096, PROT_WRITE, MAP_ANONYMOUS, -1, 0);
        assert(try_write(buf));
        munmap(buf, 4096);


        // Try successfully writing to an address, and immediately trying to
        // forbid it. If we don't TLB flush correctly, it might erroneously
        // succeed!
        buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
        *(char*)buf = 0; // write will succeed
        mprotect(buf, 4096, PROT_READ);
        assert(!try_write(buf));
        munmap(buf, 4096);

        // Test that mprotect() only hides memory, doesn't free it
        buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
        *(char*)buf = 123;
        mprotect(buf, 4096, PROT_NONE); // hide the memory - but don't unmap
        mprotect(buf, 4096, PROT_READ); // get it back
        assert(*(char*)buf == 123);

        // Test that mprotect() on malloc() memory is currently not supported
        buf = malloc(8192);
        buf = align_up(buf, 4096);
        assert(mprotect(buf, 4096, PROT_READ)==-1);
        assert(errno==ENOTSUP);
        free(buf);

        // TODO: verify that mmapping more than available physical memory doesn't
        // panic just return -1 and ENOMEM.
        // TODO: verify that huge-page-sized allocations get a huge-page aligned address
        // (if addr=0). Not critical, though, just makes sense.
        // TODO: verify that various calls to mmap() and munmap() (length=0, unaligned
        // address, etc.) fail with EINVAL.
        // TODO: test that mprotect() over malloc()ed memory (not just mmap()) works.
        debug("mmap tests succeeded\n", false);
    }
};
