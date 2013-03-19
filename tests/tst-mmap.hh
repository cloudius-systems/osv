#include "tst-hub.hh"
#include "sched.hh"
#include "debug.hh"

#include <sys/mman.h>

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

        // test mprotect. Fault-causing tests commented out until I write a
        // framework for verifing these faults.
        buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
//        debug("testing write ok");
//        *(char*)buf = 0;
//        debug("testing write failure");
        mprotect(buf, 4096, PROT_READ);
//        *(char*)buf = 0;
        munmap(buf, 4096);
        // test mprotect with part of huge page
        buf = mmap(NULL, 3*hugepagesize, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
        void *hp = (void*) (((uintptr_t)buf&~(hugepagesize-1))+hugepagesize);
        mprotect(hp+4096, 4096, PROT_READ);
//        debug("should be fine");
//        *(char*)hp = 0; // should be fine
//        debug("should be fine");
//        *(char*)(hp+8192) = 0; // should be fine
//        debug("should croak");
//        *(char*)(hp+4096) = 0; // should croak
        munmap(buf, 3*hugepagesize);

        buf = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
        //debug("testing read ok");
        //char z = *(char*)buf;
        //debug(fmt("ret %d")%z);
        //debug("testing read with PROT_READ");
        mprotect(buf, 4096, PROT_READ);
        //z = *(char*)buf ;
        //debug(fmt("ret %d")%z);
        //debug("testing read with PROT_NONE");
        mprotect(buf, 4096, PROT_NONE);
        //debug("done mprotect");
        //z = *(char*)buf ;  // should fault!
        //debug(fmt("ret %d")%z);
        munmap(buf, 4096);



        // TODO: verify that mmapping more than available physical memory doesn't
        // panic just return -1 and ENOMEM.
        // TODO: verify that huge-page-sized allocations get a huge-page aligned address
        // (if addr=0). Not critical, though, just makes sense.
        debug("mmap tests succeeded\n", false);
    }
};
