/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/numa.hh>
#include <osv/sched.hh>
#include <osv/debug.hh>

#include <unordered_map>
#include <algorithm>

#include <osv/drivers_config.h>

#if CONF_drivers_acpi
extern "C" {
#include "acpi.h"
}
#include <boost/intrusive/parent_from_member.hpp>
#endif

namespace numa {

static bool s_available = false;
static unsigned s_nr_nodes = 1;
// Map from a raw APIC id to the NUMA node it belongs to (from SRAT).
static std::unordered_map<uint32_t, unsigned> s_apic_to_node;
// Map from a sched cpu id to its NUMA node (resolved via APIC id).
static std::unordered_map<unsigned, unsigned> s_cpu_to_node;
// SLIT distance matrix, row-major, s_nr_nodes x s_nr_nodes; empty if no SLIT.
static std::vector<uint8_t> s_distances;
static std::vector<mem_range> s_mem_ranges;
static bool s_initialized = false;

unsigned nr_nodes() { return s_nr_nodes; }
bool available() { return s_available; }
const std::vector<mem_range>& memory_ranges() { return s_mem_ranges; }

unsigned node_of_cpu(unsigned cpu_id)
{
    auto it = s_cpu_to_node.find(cpu_id);
    return it == s_cpu_to_node.end() ? 0 : it->second;
}

unsigned distance(unsigned from, unsigned to)
{
    if (from == to) {
        return 10;   // ACPI convention: 10 == local.
    }
    if (!s_distances.empty() && from < s_nr_nodes && to < s_nr_nodes) {
        return s_distances[from * s_nr_nodes + to];
    }
    return 20;       // Default remote distance when no SLIT is present.
}

#if CONF_drivers_acpi
using boost::intrusive::get_parent_from_member;

// Record the (apic id -> node) mapping and track the highest node seen.
static void record_cpu_affinity(uint32_t apic_id, unsigned node, unsigned& max_node)
{
    s_apic_to_node[apic_id] = node;
    max_node = std::max(max_node, node);
}

static void parse_srat()
{
    char sig[] = ACPI_SIG_SRAT;
    ACPI_TABLE_HEADER* header;
    if (AcpiGetTable(sig, 0, &header) != AE_OK) {
        return;   // No SRAT: leave the single-node fallback in place.
    }
    auto srat = get_parent_from_member(header, &ACPI_TABLE_SRAT::Header);
    void* sub = srat + 1;
    void* end = static_cast<void*>(srat) + srat->Header.Length;
    unsigned max_node = 0;

    while (sub < end) {
        auto s = static_cast<ACPI_SUBTABLE_HEADER*>(sub);
        if (s->Length == 0) {
            break;   // Guard against a malformed zero-length subtable.
        }
        switch (s->Type) {
        case ACPI_SRAT_TYPE_CPU_AFFINITY: {
            auto a = get_parent_from_member(s, &ACPI_SRAT_CPU_AFFINITY::Header);
            if (a->Flags & ACPI_SRAT_CPU_ENABLED) {
                unsigned node = a->ProximityDomainLo |
                    (a->ProximityDomainHi[0] << 8) |
                    (a->ProximityDomainHi[1] << 16) |
                    (a->ProximityDomainHi[2] << 24);
                record_cpu_affinity(a->ApicId, node, max_node);
            }
            break;
        }
        case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY: {
            auto a = get_parent_from_member(s, &ACPI_SRAT_X2APIC_CPU_AFFINITY::Header);
            if (a->Flags & ACPI_SRAT_CPU_ENABLED) {
                record_cpu_affinity(a->ApicId, a->ProximityDomain, max_node);
            }
            break;
        }
        case ACPI_SRAT_TYPE_MEMORY_AFFINITY: {
            auto m = get_parent_from_member(s, &ACPI_SRAT_MEM_AFFINITY::Header);
            if (m->Flags & ACPI_SRAT_MEM_ENABLED) {
                s_mem_ranges.push_back(mem_range{
                    m->BaseAddress, m->Length, m->ProximityDomain,
                    (m->Flags & ACPI_SRAT_MEM_HOT_PLUGGABLE) != 0});
                max_node = std::max(max_node, (unsigned)m->ProximityDomain);
            }
            break;
        }
        default:
            break;
        }
        sub = static_cast<void*>(sub) + s->Length;
    }

    if (!s_apic_to_node.empty() || !s_mem_ranges.empty()) {
        s_available = true;
        s_nr_nodes = max_node + 1;
    }
}

static void parse_slit()
{
    char sig[] = ACPI_SIG_SLIT;
    ACPI_TABLE_HEADER* header;
    if (AcpiGetTable(sig, 0, &header) != AE_OK) {
        return;
    }
    auto slit = get_parent_from_member(header, &ACPI_TABLE_SLIT::Header);
    uint64_t n = slit->LocalityCount;
    // Only trust SLIT if it agrees with the node count we saw in SRAT.
    if (n == 0 || n != s_nr_nodes) {
        return;
    }
    s_distances.assign(slit->Entry, slit->Entry + n * n);
}

// Resolve the (apic id -> node) map into a (sched cpu id -> node) map.
static void resolve_cpus()
{
    for (auto* c : sched::cpus) {
        auto it = s_apic_to_node.find(c->arch.apic_id);
        if (it != s_apic_to_node.end()) {
            s_cpu_to_node[c->id] = it->second;
        }
    }
}
#endif

void init()
{
    if (s_initialized) {
        return;
    }
    s_initialized = true;

#if CONF_drivers_acpi
    parse_srat();
    if (s_available) {
        parse_slit();
        resolve_cpus();
    }
#endif

    if (s_available) {
        debugf("NUMA: %u node(s), %zu CPU(s) mapped, %zu memory range(s)%s\n",
               s_nr_nodes, s_cpu_to_node.size(), s_mem_ranges.size(),
               s_distances.empty() ? ", no SLIT" : "");
    } else {
        debugf("NUMA: no SRAT, assuming a single flat node\n");
    }
}

}
