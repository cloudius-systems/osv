/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Host regression: run tests/tst-numa-parser.py. Include the real translation
// unit, replacing only firmware acquisition, scheduler enumeration and logging.
// ACPICA types come from the repository's pinned submodule, not replicas.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "../core/numa.cc"

static ACPI_TABLE_HEADER* srat_table;
static ACPI_TABLE_HEADER* slit_table;

extern "C" ACPI_STATUS AcpiGetTable(char* signature, UINT32 instance,
                                    ACPI_TABLE_HEADER** out)
{
    assert(instance == 0);
    if (!strcmp(signature, ACPI_SIG_SRAT)) {
        *out = srat_table;
    } else {
        assert(!strcmp(signature, ACPI_SIG_SLIT));
        *out = slit_table;
    }
    return *out ? AE_OK : AE_NOT_FOUND;
}

using bytes = std::vector<uint8_t>;

template <typename T>
static void append(bytes& table, const T& entry, size_t length = sizeof(T))
{
    auto start = table.size();
    table.resize(start + length);
    memcpy(table.data() + start, &entry, std::min(length, sizeof(T)));
}

static ACPI_SRAT_X2APIC_CPU_AFFINITY cpu(uint32_t node = 0, uint32_t apic = 7)
{
    ACPI_SRAT_X2APIC_CPU_AFFINITY a{};
    a.Header = {ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY, sizeof(a)};
    a.ProximityDomain = node;
    a.ApicId = apic;
    a.Flags = ACPI_SRAT_CPU_ENABLED;
    return a;
}

static ACPI_SRAT_MEM_AFFINITY memory(uint64_t base, uint64_t length,
                                     uint32_t node = 0)
{
    ACPI_SRAT_MEM_AFFINITY a{};
    a.Header = {ACPI_SRAT_TYPE_MEMORY_AFFINITY, sizeof(a)};
    a.BaseAddress = base;
    a.Length = length;
    a.ProximityDomain = node;
    a.Flags = ACPI_SRAT_MEM_ENABLED | ACPI_SRAT_MEM_HOT_PLUGGABLE;
    return a;
}

static bytes srat()
{
    return bytes(sizeof(ACPI_TABLE_SRAT));
}

static bytes slit(uint64_t count, size_t entries)
{
    bytes table(offsetof(ACPI_TABLE_SLIT, Entry) + entries, 20);
    memcpy(table.data() + offsetof(ACPI_TABLE_SLIT, LocalityCount), &count,
           sizeof(count));
    return table;
}

static ACPI_TABLE_HEADER* copy_table(const bytes& table)
{
    if (table.empty()) {
        return nullptr;
    }
    assert(table.size() >= sizeof(ACPI_TABLE_HEADER));
    // Exact heap extent makes a read past the supplied firmware table visible
    // to ASan, unlike a padded stack struct or vector with spare capacity.
    auto* p = static_cast<ACPI_TABLE_HEADER*>(malloc(table.size()));
    assert(p);
    memcpy(p, table.data(), table.size());
    p->Length = table.size();
    return p;
}

static void discover(const bytes& srat_bytes, const bytes& slit_bytes = {})
{
    numa::s_initialized = false;
    numa::s_available = false;
    numa::s_nr_nodes = 1;
    numa::s_apic_to_node.clear();
    numa::s_cpu_to_node.clear();
    numa::s_mem_ranges.clear();
    numa::s_distances.clear();
    srat_table = copy_table(srat_bytes);
    slit_table = copy_table(slit_bytes);
    numa::init();
    free(srat_table);
    free(slit_table);
    srat_table = slit_table = nullptr;
}

static void fallback()
{
    assert(!numa::available());
    assert(numa::nr_nodes() == 1);
    assert(numa::memory_ranges().empty());
    assert(numa::node_of_cpu(0) == 0);
    assert(numa::node_of_cpu(1) == 0);
    assert(numa::distance(0, 1) == 20);
    assert(numa::s_apic_to_node.empty());
}

static void valid()
{
    auto table = srat();
    append(table, cpu(0));
    append(table, cpu(1, 9));
    append(table, memory(0x1000, 0x3000, 1));
    auto distances = slit(2, 4);
    auto offset = offsetof(ACPI_TABLE_SLIT, Entry);
    distances[offset] = distances[offset + 3] = 10;
    distances[offset + 1] = 30;
    distances[offset + 2] = 40;
    discover(table, distances);
    assert(numa::available() && numa::nr_nodes() == 2);
    assert(numa::node_of_cpu(0) == 0 && numa::node_of_cpu(1) == 1);
    assert(numa::distance(0, 1) == 30 && numa::distance(1, 0) == 40);
    assert(numa::distance(99, 1) == 20 && numa::distance(1, 1) == 10);
    auto& ranges = numa::memory_ranges();
    assert(ranges.size() == 1 && ranges[0].base == 0x1000);
    assert(ranges[0].length == 0x3000 && ranges[0].node == 1);
    assert(ranges[0].hotpluggable);
    numa::init(); // Idempotent, does not query the now-absent firmware tables.
    assert(numa::nr_nodes() == 2 && numa::distance(0, 1) == 30);
}

static void short_entry(uint8_t type, size_t size)
{
    for (size_t length = 2; length < size; ++length) {
        for (bool prefix : {false, true}) {
            auto table = srat();
            if (prefix) {
                append(table, cpu(1, 9));
                append(table, memory(0x1000, 0x1000, 1));
            }
            bytes entry(length);
            entry[0] = type;
            entry[1] = length;
            table.insert(table.end(), entry.begin(), entry.end());
            discover(table);
            fallback();
        }
    }
}

static void short_slit()
{
    auto table = srat();
    append(table, cpu(1, 9));
    for (size_t length = sizeof(ACPI_TABLE_HEADER);
         length < offsetof(ACPI_TABLE_SLIT, Entry); ++length) {
        discover(table, bytes(length));
        assert(numa::available() && numa::nr_nodes() == 2);
        assert(numa::distance(0, 1) == 20);
    }
}

static void domain_bound()
{
    for (uint32_t node : {UINT32_MAX}) {
        for (unsigned type : {0u, 1u, 2u}) {
            auto table = srat();
            append(table, cpu(1, 9));
            append(table, memory(0x1000, 0x1000, 1));
            if (type == 0) {
                ACPI_SRAT_CPU_AFFINITY a{};
                a.Header = {ACPI_SRAT_TYPE_CPU_AFFINITY, sizeof(a)};
                a.Flags = ACPI_SRAT_CPU_ENABLED;
                a.ProximityDomainLo = node;
                a.ProximityDomainHi[0] = node >> 8;
                a.ProximityDomainHi[1] = node >> 16;
                a.ProximityDomainHi[2] = node >> 24;
                append(table, a);
            } else if (type == 1) {
                append(table, memory(0x2000, 0x1000, node));
            } else {
                append(table, cpu(node));
            }
            discover(table);
            fallback();
        }
    }
}

static void bad_tail()
{
    for (bytes tail : {bytes{0}, bytes{0, 0}, bytes{0, 1}, bytes{0, 255}}) {
        auto table = srat();
        append(table, cpu(1, 9));
        append(table, memory(0x1000, 0x1000, 1));
        table.insert(table.end(), tail.begin(), tail.end());
        discover(table);
        fallback();
    }
    for (size_t size = sizeof(ACPI_TABLE_HEADER); size < sizeof(ACPI_TABLE_SRAT); ++size) {
        discover(bytes(size));
        fallback();
    }
}

static void ranges(bool empty)
{
    auto table = srat();
    append(table, cpu(1, 9));
    append(table, memory(UINT64_MAX - 1, empty ? 0 : 2, 1));
    discover(table);
    fallback();
}

static void boundaries()
{
    auto table = srat();
    append(table, cpu(63, 9)); // Raw domain IDs are preserved, not compacted.
    append(table, memory(UINT64_MAX - 1, 1, 63)); // Non-wrapping upper boundary.
    ACPI_SRAT_CPU_AFFINITY legacy{};
    legacy.Header = {ACPI_SRAT_TYPE_CPU_AFFINITY, sizeof(legacy)};
    legacy.Flags = ACPI_SRAT_CPU_ENABLED;
    legacy.ApicId = 7;
    legacy.ProximityDomainLo = 1;
    append(table, legacy);
    discover(table);
    assert(numa::available() && numa::nr_nodes() == 64);
    assert(numa::node_of_cpu(0) == 1 && numa::node_of_cpu(1) == 63);
    assert(numa::memory_ranges().size() == 1);
    auto distances = slit(64, 4096);
    distances[offsetof(ACPI_TABLE_SLIT, Entry) + 63] = 42;
    discover(table, distances);
    assert(numa::distance(0, 63) == 42);
    assert(numa::distance(63, 64) == 20);
}

// A raw ID is not a node count or a CPU count. Removing support for a
// well-formed sparse topology must fail these compatibility tests.
static void sparse()
{
    for (auto ids : {std::pair<uint32_t, uint32_t>{0, 128}, {63, 64}}) {
        auto table = srat();
        append(table, cpu(ids.first, 7));
        append(table, cpu(ids.second, 9));
        append(table, memory(0x1000, 0x1000, ids.first));
        append(table, memory(0x2000, 0x1000, ids.second));
        uint64_t n = ids.second + 1;
        auto distances = slit(n, n * n);
        auto offset = offsetof(ACPI_TABLE_SLIT, Entry);
        distances[offset + ids.first * n + ids.second] = 31;
        distances[offset + ids.second * n + ids.first] = 47;
        discover(table, distances);
        assert(numa::available() && numa::nr_nodes() == n);
        assert(numa::node_of_cpu(0) == ids.first);
        assert(numa::node_of_cpu(1) == ids.second);
        assert(numa::memory_ranges().size() == 2);
        assert(numa::memory_ranges()[0].node == ids.first);
        assert(numa::memory_ranges()[1].node == ids.second);
        assert(numa::distance(ids.first, ids.second) == 31);
        assert(numa::distance(ids.second, ids.first) == 47);
    }
    auto table = srat();
    append(table, cpu(UINT32_MAX - 1, 9));
    append(table, memory(0x1000, 0x1000, UINT32_MAX - 1));
    discover(table);
    assert(numa::available() && numa::nr_nodes() == UINT32_MAX);
    assert(numa::node_of_cpu(1) == UINT32_MAX - 1);
    assert(numa::memory_ranges()[0].node == UINT32_MAX - 1);
    assert(numa::distance(0, UINT32_MAX - 1) == 20);
    // Matching huge count still needs a complete matrix. Do not multiply or
    // allocate from it before checking the remaining firmware table extent.
    discover(table, slit(UINT32_MAX, 0));
    assert(numa::available() && numa::nr_nodes() == UINT32_MAX);
    assert(numa::distance(0, UINT32_MAX - 1) == 20);
}

static void ignored()
{
    discover({});
    fallback();
    auto table = srat();
    auto a = cpu(UINT32_MAX);
    a.Flags = 0;
    append(table, a);
    auto m = memory(UINT64_MAX, UINT64_MAX, UINT32_MAX);
    m.Flags = 0;
    append(table, m);
    append(table, ACPI_SUBTABLE_HEADER{255, 2}); // Well-framed unknown type.
    discover(table);
    fallback();
    a = cpu(1, 9);
    a.Header.Length += 4; // Forward-compatible extension of a known record.
    append(table, a, a.Header.Length);
    discover(table);
    assert(numa::available() && numa::nr_nodes() == 2);
    assert(numa::node_of_cpu(1) == 1);
}

static void slit_bounds()
{
    auto table = srat();
    append(table, cpu(1, 9));
    for (uint64_t n : std::initializer_list<uint64_t>{0, 1, 3, 64, 0x100000000ULL, UINT64_MAX}) {
        discover(table, slit(n, 4));
        assert(numa::available() && numa::nr_nodes() == 2);
        assert(numa::distance(0, 1) == 20);
    }
    for (size_t length = 0; length < 4; ++length) {
        discover(table, slit(2, length));
        assert(numa::available() && numa::nr_nodes() == 2);
        assert(numa::distance(0, 1) == 20);
    }
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    struct test { const char* name; void (*run)(); } tests[] = {
        {"valid", valid},
        {"short-cpu", [] { short_entry(ACPI_SRAT_TYPE_CPU_AFFINITY, sizeof(ACPI_SRAT_CPU_AFFINITY)); }},
        {"short-x2apic", [] { short_entry(ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY, sizeof(ACPI_SRAT_X2APIC_CPU_AFFINITY)); }},
        {"short-memory", [] { short_entry(ACPI_SRAT_TYPE_MEMORY_AFFINITY, sizeof(ACPI_SRAT_MEM_AFFINITY)); }},
        {"short-slit", short_slit}, {"domain-bound", domain_bound},
        {"max-pxm", [] {
            auto table = srat();
            append(table, cpu(UINT32_MAX));
            discover(table);
            assert(numa::nr_nodes() >= 1);
            fallback();
        }},
        {"bad-tail", bad_tail}, {"overflow-memory", [] { ranges(false); }},
        {"empty-memory", [] { ranges(true); }}, {"boundaries", boundaries},
        {"sparse", sparse}, {"ignored", ignored}, {"slit-bounds", slit_bounds},
    };
    for (auto& test : tests) {
        if (!strcmp(argv[1], test.name)) {
            test.run();
            printf("PASS %s\n", test.name);
            return 0;
        }
    }
    return 2;
}
