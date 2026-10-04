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

// Boot discovery implementation budgets, not firmware validity or CPU limits.
// A 64KiB compact distance matrix permits 256 distinct CPU/memory domains.
// The SRAT byte limit bounds parsing and staged metadata growth separately.
// These are NOT a budget for per-node allocator pools or worker threads.
constexpr unsigned max_nodes = 256;
constexpr size_t max_srat_bytes = 1024 * 1024;

// A contiguous physical memory range assigned to a NUMA node.
struct mem_range {
    uint64_t base;
    uint64_t length;
    unsigned node;
    bool     hotpluggable;
};

// Discover the topology by parsing SRAT/SLIT.  Safe to call once, after ACPI is
// initialized and the CPUs have been enumerated (so APIC ids are known).  If no
// SRAT is present, or it is malformed/unsupported, initializes a single flat
// node. Over-budget or allocation-failed discovery also gives this fallback.
// Invalid SLIT leaves SRAT intact with default distances; no matrix indexed by
// raw PXM is allocated. IDs are sorted by raw PXM, stable across record order.
// Idempotent; boot-only publication, not concurrent runtime reinitialization.
void init();

// Number of discovered distinct domains (CPU and/or memory), always >= 1.
// Node IDs are dense [0, nr_nodes()), not firmware proximity-domain numbers.
unsigned nr_nodes();

// True if the topology came from a real SRAT (as opposed to the synthesized
// single-node fallback).
bool available();

// Firmware provenance for a dense node; false for the synthetic fallback or
// invalid node. Does not manufacture raw PXM 0 for unknown topology.
bool raw_domain(unsigned node, uint32_t& domain);

// The node a CPU (by sched cpu id) belongs to, or 0 if unknown. The compatibility
// fallback is not evidence of affinity to dense0 (which need not be raw PXM0).
bool cpu_node_known(unsigned cpu_id);
unsigned node_of_cpu(unsigned cpu_id);

// The SLIT distance from node `from` to node `to`.  Linux/ACPI convention:
// The diagonal is 10; off-diagonal values are >= 10, with 255 unreachable.
// Returns defaults (10 local / 20 remote) when no SLIT is present.
unsigned distance(unsigned from, unsigned to);

// The memory ranges discovered from SRAT (empty if none).
const std::vector<mem_range>& memory_ranges();

}

#endif
