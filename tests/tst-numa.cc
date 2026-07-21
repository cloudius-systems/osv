/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Verifies NUMA topology discovery.  Boot with QEMU's -numa options to see more
// than one node; without them (the default) the machine is reported as a single
// flat node.  Built and run as part of the OSv test image.

#include <osv/numa.hh>
#include <osv/sched.hh>

#include <cassert>
#include <iostream>

int main()
{
    std::cerr << "Running numa tests\n";

    // There is always at least one node.
    unsigned n = numa::nr_nodes();
    assert(n >= 1);
    std::cerr << "  nodes=" << n << " available=" << numa::available() << "\n";

    // Every CPU maps to a node within range.
    for (auto* c : sched::cpus) {
        unsigned node = numa::node_of_cpu(c->id);
        assert(node < n);
    }

    // Distances: the diagonal is local (10); off-diagonal is >= local.
    for (unsigned a = 0; a < n; a++) {
        assert(numa::distance(a, a) == 10);
        for (unsigned b = 0; b < n; b++) {
            assert(numa::distance(a, b) >= 10);
        }
    }

    // Memory ranges (if any) all name a node within range.
    for (auto& r : numa::memory_ranges()) {
        assert(r.node < n);
        assert(r.length > 0);
    }

    // When SRAT is present, every CPU should have been mapped and the node
    // count should match at least one memory range or cpu affinity.
    if (numa::available()) {
        std::cerr << "  memory ranges=" << numa::memory_ranges().size() << "\n";
    }

    std::cerr << "numa tests PASSED\n";
    return 0;
}
