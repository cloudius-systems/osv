/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_NUMA_HH
#define OSV_NUMA_HH

#include <cstdint>
#include <vector>
#include <cstddef>

// NUMA topology discovery.
//
// This module parses the ACPI SRAT (System Resource Affinity Table) and SLIT
// (System Locality Distance Information Table) to learn the machine's NUMA
// layout: which node each CPU belongs to, which physical memory ranges belong
// to each node, and the relative distances between nodes.
//
// This is discovery only: it does not change how memory is allocated or how
// threads are scheduled.  It exposes the topology so later work (a node-aware
// allocator, scheduler affinity, mbind/get_mempolicy) can use it.  On a machine
// with no SRAT (the common single-node virtual machine), the whole system is
// reported as one node (node 0) containing every CPU.

namespace numa {

// A contiguous physical memory range assigned to a NUMA node.
struct mem_range {
    uint64_t base;
    uint64_t length;
    unsigned node;
    bool     hotpluggable;
};

// Discover the topology by parsing SRAT/SLIT.  Safe to call once, after ACPI is
// initialized and the CPUs have been enumerated (so APIC ids are known).  If no
// SRAT is present, initializes a single flat node.  Idempotent.
void init();

// Number of NUMA nodes (>= 1).
unsigned nr_nodes();

// True if the topology came from a real SRAT (as opposed to the synthesized
// single-node fallback).
bool available();

// The node a CPU (by sched cpu id) belongs to, or 0 if unknown.
unsigned node_of_cpu(unsigned cpu_id);

// The node that owns a physical address, or -1 if it falls in no known range.
// Only meaningful when available() is true.
int node_of_phys(uint64_t phys);

// The SLIT distance from node `from` to node `to`.  Linux/ACPI convention:
// 10 == local (same node), higher == farther.  Returns 10 for the diagonal and
// a default (10 local / 20 remote) when no SLIT is present.
unsigned distance(unsigned from, unsigned to);

// The memory ranges discovered from SRAT (empty if none).
const std::vector<mem_range>& memory_ranges();

}

#endif
