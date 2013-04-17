#include "sched.hh"
#include "debug.hh"
#include <sys/mman.h>

namespace memory {
extern bool tracker_enabled;
}

int main(int argc, char **argv)
{
    bool save_tracker_enabled = memory::tracker_enabled;
    memory::tracker_enabled = true;

    debug("testing leaks in malloc\n");
    for (int i = 0; i < 100; i++) {
        memory::free_page(memory::alloc_page());
    }

    int sizes[] = {4, 888, 4336, 65536};
    for (int size : sizes) {
        for(int i=0; i<100; i++) {
            free(malloc(size));
        }
    }

    debug("testing leaks in mmap\n");
    int mmap_sizes[] = {4096, 4096, 4096*512 };
    for (int size : mmap_sizes) {
        for (int i=0; i < 100; i++) {
            void *buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
            assert(buf);
            munmap(buf, size);
        }
    }

    debug("testing leaks in threads\n");
    for(int i=0; i<100; i++){
        sched::thread *t = new sched::thread([] {});
        delete t;
    }
    for(int i=0; i<100; i++){
        sched::thread *t = new sched::thread([] {});
        t->start();
        t->join();
        delete t;
    }
    debug("leak testing done. Please use 'osv leak show' in gdb to analyze results\n");

    memory::tracker_enabled = save_tracker_enabled;
}
