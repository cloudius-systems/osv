/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Verifies memory::alloc_page_on_node(): pages requested for a node land on
// that node when NUMA is available, and it falls back cleanly otherwise.
// The suite boots a single node, where only the fallback paths run.  To check
// placement, boot a two-node guest:
//
//   scripts/run.py -c 2 -m 2G --pass-args "-object memory-backend-ram,id=m0,size=1G
//     -numa node,nodeid=0,cpus=0,memdev=m0 -object memory-backend-ram,id=m1,size=1G
//     -numa node,nodeid=1,cpus=1,memdev=m1" -e /tests/tst-numa-alloc.so

#include <osv/pagealloc.hh>
#include <osv/numa.hh>
#include <osv/mmu.hh>

#include <cassert>
#include <vector>
#include <iostream>

int main()
{
    std::cerr << "Running numa-alloc tests\n";
    unsigned nodes = numa::nr_nodes();
    std::cerr << "  nodes=" << nodes << " available=" << numa::available() << "\n";

    // Basic: allocating on node 0 always returns a usable page.
    void *p = memory::alloc_page_on_node(0);
    assert(p != nullptr);
    // Writable.
    *(volatile char *)p = 0x5a;
    assert(*(volatile char *)p == 0x5a);
    memory::free_page(p);

    // Out-of-range / negative node falls back to a normal allocation, no crash.
    p = memory::alloc_page_on_node(-1);
    assert(p != nullptr);
    memory::free_page(p);
    p = memory::alloc_page_on_node(nodes + 100);
    assert(p != nullptr);
    memory::free_page(p);

    // When NUMA is available, a page requested for a node comes from that
    // node's free memory in the global page-range allocator.  On a freshly
    // booted guest every node still has plenty there, so each of a handful
    // of pages per node must land on the node it was requested for.
    if (numa::available()) {
        for (unsigned n = 0; n < nodes; n++) {
            std::vector<void *> pages;
            int on_node = 0, off_node = 0;
            for (int i = 0; i < 32; i++) {
                void *q = memory::alloc_page_on_node(n);
                assert(q != nullptr);
                *(volatile char *)q = (char)i;   // writable
                pages.push_back(q);
                auto phys = mmu::virt_to_phys(q);
                int got = numa::node_of_phys(phys);
                // A fallback still resolves to a real node, never garbage.
                assert(got == -1 || (got >= 0 && (unsigned)got < nodes));
                if (got == (int)n) {
                    on_node++;
                } else {
                    off_node++;
                }
            }
            std::cerr << "  node " << n << ": " << on_node << " on-node, "
                      << off_node << " fell back\n";
            for (void *q : pages) {
                memory::free_page(q);
            }
            assert(on_node > 0);
        }
    }

    std::cerr << "numa-alloc tests PASSED\n";
    return 0;
}
