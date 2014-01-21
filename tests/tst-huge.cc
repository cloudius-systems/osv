/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifdef __OSV__
#include <osv/sched.hh>
#include "mempool.hh"
#include "mmu.hh"
#include "barrier.hh"
#endif

#include <sys/mman.h>
#include <signal.h>

#include <iostream>
#include <cassert>
#include <cstdlib>

static unsigned char* align_page_up(void *x)
{
    uintptr_t addr = reinterpret_cast<uintptr_t>(x);

    return reinterpret_cast<unsigned char *>((addr + 4095UL) & ~4095UL);
}

void *exhaust_memory(size_t size)
{
    void *addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    unsigned char *ptr = static_cast<unsigned char *>(addr);
    unsigned char *p = ptr;

    p = align_page_up(ptr);
    while (p < ptr + size - mmu::huge_page_size) {
        munmap(p + mmu::page_size, 511 * mmu::page_size);
        p += mmu::huge_page_size;
    }
    return addr;
}

void destroy_maps(void *addr, size_t size)
{
    unsigned char *ptr = static_cast<unsigned char *>(addr);
    unsigned char *p = ptr;

    p = align_page_up(ptr);
    munmap(ptr, p - ptr);
    while (p < ptr + size - mmu::huge_page_size) {
        munmap(p, mmu::page_size);
        p += mmu::huge_page_size;
    }

    if (p < ptr + size) {
        munmap(p, (ptr + size) - p);
    }
}

void do_test(size_t size)
{
    void *buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_POPULATE|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    assert(buf);
    unsigned int *addr = static_cast<unsigned int *>(buf);

    for (unsigned int j = 0; j < size/sizeof(j); j += 4096 / sizeof(j)) {
        addr[j] = j;
        barrier();
        assert(addr[j] == j);
    }
    munmap(buf, size);
}

// If we try to allocate a huge page and fail, we will fill the space with small pages.
// Test to see if that mechanism is working properly. We could just go allocating 2Mb pages
// until we run out of them, but since our memory space starts quite unfragmented, reaching
// that will make us go inside the reserve pools and halt.
int main(int argc, char **argv)
{
    std::cerr << "Running huge tests\n";

#ifdef __OSV__
    size_t size = 128 << 20;
    // First make sure that everything is okay. In this step, we should have
    // 128 Mb worth of huge pages, since free memory is plentiful. We should be
    // able to write to them and read the result back.
    do_test(size);

    std::vector<void *> addresses;
    std::vector<void *> hpages;
    void *addr;

    // We will now go over all memory, maping 128Mb regions but freeing 511
    // pages at each 2mb interval. After this, memory should be plentiful, but
    // finding a 2Mb region is going to be very hard to find. After each mmap allocation,
    // we allocate a huge page directly. If we fail, it is time to stop.
    do {
        addresses.push_back(exhaust_memory(size));
        addr = memory::alloc_huge_page(mmu::huge_page_size);
        if (addr) {
            hpages.push_back(addr);
        }
    } while(addr);

    // We will map a 128Mb array again. There are no more huge pages, so we
    // will fail. The mapping will consist of small pages filling the space of
    // huge pages. If everything works, we should have no problems reading back
    // the values we wrote to the mapping.
    do_test(size);

    // Clean up so we have the system back into place.
    while (!addresses.empty()) {
        destroy_maps(addresses.back(), size);
        addresses.pop_back();
    }

    while (!hpages.empty()) {
        memory::free_huge_page(hpages.back(), mmu::huge_page_size);
        hpages.pop_back();
    }
#endif
    std::cerr << "huge tests succeeded\n";
    return 0;
}
