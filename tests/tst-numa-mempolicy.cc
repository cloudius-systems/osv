/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Verifies that getcpu and get_mempolicy report the NUMA topology discovered by
// the numa:: module (rather than a hardcoded single node).  Boot with QEMU's
// -numa options to exercise more than one node.  Built and run as part of the
// OSv test image.

#include <osv/numa.hh>

#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <cassert>
#include <iostream>

#define MPOL_F_NODE (1 << 0)
#define MPOL_F_ADDR (1 << 1)

static long get_mempolicy_(int *policy, unsigned long *nmask,
        unsigned long maxnode, void *addr, int flags)
{
    return syscall(SYS_get_mempolicy, policy, nmask, maxnode, addr, flags);
}

int main()
{
    std::cerr << "Running numa-mempolicy tests\n";
    unsigned nodes = numa::nr_nodes();

    // getcpu (via sched_getcpu, which passes a node-out pointer) must report a
    // node within range and consistent with numa::node_of_cpu.
    unsigned cpu = 0, node = 0;
    long r = syscall(SYS_getcpu, &cpu, &node, nullptr);
    assert(r == 0);
    assert(node < nodes);
    assert(node == numa::node_of_cpu(cpu));

    // get_mempolicy with MPOL_F_NODE reports the current CPU's node.
    int cur_node = -1;
    assert(get_mempolicy_(&cur_node, nullptr, 0, nullptr, MPOL_F_NODE) == 0);
    assert(cur_node >= 0 && (unsigned)cur_node < nodes);

    // The allowed-nodes mask must have exactly `nodes` bits set at minimum, and
    // maxnode too small must fail with EINVAL.
    unsigned long mask[16] = {};
    int policy = -1;
    assert(get_mempolicy_(&policy, mask, sizeof(mask) * 8, nullptr, 0) == 0);
    int bits = 0;
    for (unsigned i = 0; i < sizeof(mask) * 8; i++) {
        if (mask[i / (8 * sizeof(unsigned long))] &
            (1UL << (i % (8 * sizeof(unsigned long))))) {
            bits++;
        }
    }
    assert((unsigned)bits == nodes);

    if (nodes > 1) {
        errno = 0;
        assert(get_mempolicy_(&policy, mask, 1, nullptr, 0) == -1 &&
               errno == EINVAL);
    }

    // MPOL_F_NODE|MPOL_F_ADDR reports the node backing a given address.  On a
    // real NUMA machine this must be a valid node; the value depends on
    // placement so we only check the range.
    void *buf = malloc(4096);
    assert(buf);
    *(volatile char *)buf = 1;  // fault it in
    int addr_node = -1;
    assert(get_mempolicy_(&addr_node, nullptr, 0, buf, MPOL_F_NODE | MPOL_F_ADDR) == 0);
    assert(addr_node >= 0 && (unsigned)addr_node < nodes);
    free(buf);

    std::cerr << "  nodes=" << nodes << " current_node=" << cur_node << "\n";
    std::cerr << "numa-mempolicy tests PASSED\n";
    return 0;
}
