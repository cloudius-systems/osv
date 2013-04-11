#ifndef __TST_LEAK__
#define __TST_LEAK__

#include "tst-hub.hh"
#include "sched.hh"
#include "debug.hh"

class test_leak : public unit_tests::vtest {

public:
    void run()
    {
        size_t total, newtotal, contig;
        debug("testing leaks in malloc\n");
        memory::debug_memory_pool(&total, &contig);
        for (int i = 0; i < 100; i++) {
            memory::free_page(memory::alloc_page());
        }
        memory::debug_memory_pool(&newtotal, &contig);
        assert(total==newtotal);

        int sizes[] = {4, 888, 4336, 65536};
        for (int size : sizes) {
            memory::debug_memory_pool(&total, &contig);
            for(int i=0; i<100; i++) {
                free(malloc(size));
            }
            memory::debug_memory_pool(&newtotal, &contig);
            // Allow to leak one page (in case malloc needed a new page for
            // this size) but not more than that.
            assert(newtotal>=total-4096);
        }

        debug("testing leaks in threads\n");
        memory::debug_memory_pool(&total, &contig);
        for(int i=0; i<100; i++){
            sched::thread *t = new sched::thread([] {});
            delete t;
        }
        memory::debug_memory_pool(&newtotal, &contig);
        assert(newtotal>=total-4096);

        memory::debug_memory_pool(&total, &contig);
        for(int i=0; i<100; i++){
            sched::thread *t = new sched::thread([] {});
            t->start();
            t->join();
            delete t;
        }
        memory::debug_memory_pool(&newtotal, &contig);
        assert(newtotal>=total-4096);

        debug("leak testing done\n");
    }
};

#endif
