/*
 * Copyright (C) 2026 Greg Burd
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
#include <cstdlib>

int main(int argc, char** argv)
{
    std::cerr << "Running numa tests\n";

    // There is always at least one node.
    unsigned n = numa::nr_nodes();
    assert(n >= 1 && n <= numa::max_nodes);
    if (argc > 1) {
        assert(n == std::strtoul(argv[1], nullptr, 0));
    }
    if (argc > 2) {
        assert(numa::available() == bool(std::strtoul(argv[2], nullptr, 0)));
    }
    for (unsigned node = 0; node < n; ++node) {
        uint32_t raw = 0;
        bool known = numa::raw_domain(node, raw);
        assert(known == numa::available());
        std::cerr << "  dense=" << node << " raw-known=" << known << " raw=" << raw << "\n";
    }
    std::cerr << "  nodes=" << n << " available=" << numa::available() << "\n";

    // Every CPU maps to a node within range.
    for (auto* c : sched::cpus) {
        unsigned node = numa::node_of_cpu(c->id);
        assert(node < n);
        std::cerr << "  cpu=" << c->id << " node=" << node
                  << " known=" << numa::cpu_node_known(c->id) << "\n";
        if (numa::cpu_node_known(c->id)) {
            uint32_t raw;
            assert(numa::available() && numa::raw_domain(node, raw));
        } else {
            assert(node == 0); // Compatibility fallback, not known affinity.
        }
    }

    // Distances: the diagonal is local (10); off-diagonal is >= local.
    for (unsigned a = 0; a < n; a++) {
        assert(numa::distance(a, a) == 10);
        for (unsigned b = 0; b < n; b++) {
            assert(numa::distance(a, b) >= 10);
            if (argc > 3 && a != b && n == 2) {
                assert(numa::distance(a, b) == (a == 0 ? 31u : 47u));
            }
        }
    }

    // Memory ranges (if any) all name a node within range.
    for (auto& r : numa::memory_ranges()) {
        assert(r.node < n);
        assert(r.length > 0);
    }

    // Firmware ranges report ownership, not usable/free allocator capacity.
    if (numa::available()) {
        std::cerr << "  memory ranges=" << numa::memory_ranges().size() << "\n";
    }

    std::cerr << "numa tests PASSED\n";
    return 0;
}
