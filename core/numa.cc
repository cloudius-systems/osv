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
#include <cstddef>
#include <cstdint>
#include <new>

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
struct topology {
    // Retain the inverse mapping: SLIT rows use raw PXM, never dense IDs.
    std::vector<uint32_t> domains;
    std::unordered_map<uint32_t, unsigned> apic_to_node;
    std::unordered_map<unsigned, unsigned> cpu_to_node;
    std::vector<uint8_t> distances;
    std::vector<mem_range> mem_ranges;
};
static topology s_topology;
static bool s_initialized = false;

unsigned nr_nodes() { return s_nr_nodes; }
bool available() { return s_available; }
const std::vector<mem_range>& memory_ranges() { return s_topology.mem_ranges; }

unsigned node_of_cpu(unsigned cpu_id)
{
    auto it = s_topology.cpu_to_node.find(cpu_id);
    return it == s_topology.cpu_to_node.end() ? 0 : it->second;
}

bool cpu_node_known(unsigned cpu_id)
{
    return s_topology.cpu_to_node.find(cpu_id) != s_topology.cpu_to_node.end();
}

bool raw_domain(unsigned node, uint32_t& domain)
{
    if (node >= s_topology.domains.size()) {
        return false;
    }
    domain = s_topology.domains[node];
    return true;
}

unsigned distance(unsigned from, unsigned to)
{
    if (from == to) {
        return 10;   // ACPI convention: 10 == local.
    }
    if (!s_topology.distances.empty() && from < s_nr_nodes && to < s_nr_nodes) {
        return s_topology.distances[from * s_nr_nodes + to];
    }
    return 20;       // Default remote distance when no SLIT is present.
}

#if CONF_drivers_acpi
using boost::intrusive::get_parent_from_member;

static bool parse_srat(topology& t)
{
    char sig[] = ACPI_SIG_SRAT;
    ACPI_TABLE_HEADER* header;
    if (AcpiGetTable(sig, 0, &header) != AE_OK) {
        return false;   // No SRAT: leave the single-node fallback in place.
    }
    auto srat = get_parent_from_member(header, &ACPI_TABLE_SRAT::Header);
    if (srat->Header.Length < sizeof(ACPI_TABLE_SRAT) ||
        srat->Header.Length > max_srat_bytes) {
        return false;   // Truncated SRAT header: nothing safe to walk.
    }
    // Walk with a byte cursor: void* arithmetic is a non-standard GNU extension,
    // and a byte cursor makes the bounds checks below straightforward.
    auto* base = reinterpret_cast<char*>(srat);
    auto* cur = base + sizeof(ACPI_TABLE_SRAT);
    auto* end = base + srat->Header.Length;
    std::unordered_map<uint32_t, unsigned> apic_to_node;
    std::vector<mem_range> mem_ranges;
    auto record_domain = [&](uint32_t domain) {
        if (std::find(t.domains.begin(), t.domains.end(), domain) == t.domains.end()) {
            if (t.domains.size() == max_nodes) {
                return false;
            }
            t.domains.push_back(domain);
        }
        return true;
    };
    auto record_cpu_affinity = [&](uint32_t apic_id, unsigned node) {
        if (!record_domain(node)) {
            return false;
        }
        auto entry = apic_to_node.emplace(apic_id, node);
        return entry.second || entry.first->second == node;
    };

    // Stage the topology until every entry has been checked. A malformed tail
    // must not publish a partial CPU/memory map.
    while (cur < end) {
        if (size_t(end - cur) < sizeof(ACPI_SUBTABLE_HEADER)) {
            return false;
        }
        auto s = reinterpret_cast<ACPI_SUBTABLE_HEADER*>(cur);
        if (s->Length < sizeof(ACPI_SUBTABLE_HEADER) || s->Length > size_t(end - cur)) {
            return false;   // Malformed: zero/short length, or subtable overruns SRAT.
        }
        switch (s->Type) {
        case ACPI_SRAT_TYPE_CPU_AFFINITY: {
            if (s->Length < sizeof(ACPI_SRAT_CPU_AFFINITY)) {
                return false;
            }
            auto a = get_parent_from_member(s, &ACPI_SRAT_CPU_AFFINITY::Header);
            if (a->Flags & ACPI_SRAT_CPU_ENABLED) {
                unsigned node = a->ProximityDomainLo |
                    (a->ProximityDomainHi[0] << 8) |
                    (a->ProximityDomainHi[1] << 16) |
                    (uint32_t(a->ProximityDomainHi[2]) << 24);
                if (!record_cpu_affinity(a->ApicId, node)) {
                    return false;
                }
            }
            break;
        }
        case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY: {
            if (s->Length < sizeof(ACPI_SRAT_X2APIC_CPU_AFFINITY)) {
                return false;
            }
            auto a = get_parent_from_member(s, &ACPI_SRAT_X2APIC_CPU_AFFINITY::Header);
            if (a->Flags & ACPI_SRAT_CPU_ENABLED) {
                if (!record_cpu_affinity(a->ApicId, a->ProximityDomain)) {
                    return false;
                }
            }
            break;
        }
        case ACPI_SRAT_TYPE_MEMORY_AFFINITY: {
            if (s->Length < sizeof(ACPI_SRAT_MEM_AFFINITY)) {
                return false;
            }
            auto m = get_parent_from_member(s, &ACPI_SRAT_MEM_AFFINITY::Header);
            if (m->Flags & ACPI_SRAT_MEM_ENABLED) {
                if (!record_domain(m->ProximityDomain) || m->Length == 0 ||
                    m->Length > UINT64_MAX - m->BaseAddress) {
                    return false;
                }
                mem_ranges.push_back(mem_range{
                    m->BaseAddress, m->Length, m->ProximityDomain,
                    (m->Flags & ACPI_SRAT_MEM_HOT_PLUGGABLE) != 0});
            }
            break;
        }
        default:
            break;
        }
        cur += s->Length;
    }

    if (t.domains.empty()) {
        return false;
    }
    std::sort(t.domains.begin(), t.domains.end());
    auto dense = [&](uint32_t raw) {
        return unsigned(std::lower_bound(t.domains.begin(), t.domains.end(), raw)
                        - t.domains.begin());
    };
    for (auto& entry : apic_to_node) {
        entry.second = dense(entry.second);
    }
    std::sort(mem_ranges.begin(), mem_ranges.end(),
              [](const mem_range& a, const mem_range& b) { return a.base < b.base; });
    uint64_t end_address = 0;
    for (auto& range : mem_ranges) {
        if (range.base < end_address) {
            return false; // No ambiguous physical ownership, even within one node.
        }
        end_address = range.base + range.length; // Validated above.
        range.node = dense(range.node);
    }
    t.apic_to_node = std::move(apic_to_node);
    t.mem_ranges = std::move(mem_ranges);
    return true;
}

static void parse_slit(topology& t)
{
    char sig[] = ACPI_SIG_SLIT;
    ACPI_TABLE_HEADER* header;
    if (AcpiGetTable(sig, 0, &header) != AE_OK) {
        return;
    }
    constexpr size_t fixed_size = offsetof(ACPI_TABLE_SLIT, Entry);
    if (header->Length < fixed_size) {
        return;
    }
    auto slit = get_parent_from_member(header, &ACPI_TABLE_SLIT::Header);
    uint64_t n = slit->LocalityCount;
    // LocalityCount is a raw-domain extent, not the compact node count.
    if (n == 0 || t.domains.back() >= n) {
        return;
    }
    // Guard against a malformed/truncated SLIT: the n*n entries must actually
    // fit within the table's declared length (and n*n must not overflow).
    if (n > (header->Length - fixed_size) / n) {
        return;
    }
    auto count = t.domains.size();
    std::vector<uint8_t> distances(count * count);
    for (size_t from = 0; from < count; ++from) {
        for (size_t to = 0; to < count; ++to) {
            auto value = slit->Entry[uint64_t(t.domains[from]) * n + t.domains[to]];
            // ACPI 6.5 section 5.2.17: diagonal 10, 0..9 reserved,
            // 255 unreachable. Off-diagonal 10 is not forbidden.
            if ((from == to && value != 10) || value < 10) {
                return;
            }
            distances[from * count + to] = value;
        }
    }
    t.distances = std::move(distances);
}

// Resolve the (apic id -> node) map into a (sched cpu id -> node) map.
static void resolve_cpus(topology& t)
{
    for (auto* c : sched::cpus) {
        auto it = t.apic_to_node.find(c->arch.apic_id);
        if (it != t.apic_to_node.end()) {
            t.cpu_to_node[c->id] = it->second;
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
    try {
        topology pending;
        if (parse_srat(pending)) {
            parse_slit(pending);
            resolve_cpus(pending);
            // All allocations have completed. Boot-only publication, not a
            // protocol for concurrent readers or runtime reinitialization.
            s_topology = std::move(pending);
            s_nr_nodes = s_topology.domains.size();
            s_available = true;
        }
    } catch (const std::bad_alloc&) {
        // Synthetic/unknown topology is preferable to a partial firmware map.
    }
#endif

    if (s_available) {
        debugf("NUMA: %u node(s), %zu CPU(s) mapped, %zu memory range(s)%s\n",
               s_nr_nodes, s_topology.cpu_to_node.size(), s_topology.mem_ranges.size(),
               s_topology.distances.empty() ? ", no SLIT" : "");
    } else {
        debugf("NUMA: no SRAT, assuming a single flat node\n");
    }
}

}
