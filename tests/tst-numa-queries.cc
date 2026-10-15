/* Copyright (C) 2026 Greg Burd. BSD license, see LICENSE. */
#include <cassert>
#include <cerrno>
#include <climits>
#include <cstring>
#include <cstdio>
#include "../core/numa.cc"
#include "query-bodies.hh"

static void topology(unsigned count, bool available,
                     std::initializer_list<numa::mem_range> ranges)
{
    numa::s_topology = {};
    numa::s_nr_nodes = count;
    numa::s_available = available;
    numa::s_topology.mem_ranges = ranges;
}

static void error(long rc, int expected)
{
    assert(rc == -1 && errno == expected);
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const char* test = argv[1];
    topology(1, false, {});
    unsigned long m[5];
    for (auto& word : m) { word = ~0UL; }
    int p = -77;
    if (!strcmp(test, "default")) {
        assert(get_mempolicy(&p, m, 66, nullptr, 0) == 0);
        assert(p == 0 && m[0] == 0 && m[1] == 0 && m[2] == ~0UL);
        assert(get_mempolicy(nullptr, nullptr, ULONG_MAX, nullptr, 0) == 0);
    } else if (!strcmp(test, "allowed")) {
        topology(3, true, {{0x1000, 0x1000, 1, false}, {0x4000, 0x1000, 2, false}});
        assert(get_mempolicy(&p, m, 65, nullptr, 4) == 0);
        assert(p == 0 && m[0] == 6 && m[1] == ~0UL); // CPU-only0 excluded.
        topology(3, true, {});
        error(get_mempolicy(&p, m, 65, nullptr, 4), EOPNOTSUPP);
        assert(get_mempolicy(&p, m, 65, nullptr, 0) == 0 && m[0] == 0);
        topology(1, false, {});
        assert(get_mempolicy(&p, m, 65, reinterpret_cast<void*>(1), 4) == 0);
        assert(m[0] == 1); // MEMS_ALLOWED ignores addr, Linux early return.
        topology(256, true, {{0x1000, 0x1000, 255, false}});
        assert(get_mempolicy(nullptr, m, 257, nullptr, 4) == 0);
        assert(m[0] == 0 && m[1] == 0 && m[2] == 0 && m[3] == (1UL<<63));
        assert(m[4] == ~0UL);
    } else if (!strcmp(test, "bounds")) {
        error(get_mempolicy(&p, m, 0, nullptr, 4), EINVAL);
        assert(p == -77 && m[0] == ~0UL);
        for (unsigned long bits : {1UL, 63UL, 64UL, 65UL, 66UL}) {
            for (auto& word : m) { word = ~0UL; }
            assert(get_mempolicy(&p, m, bits, nullptr, 4) == 0);
            assert(m[0] == (bits == 1 ? ~0UL : 1));
            assert(m[1] == (bits == 66 ? 0 : ~0UL));
            assert(m[2] == ~0UL);
        }
        topology(66, true, {{0, 1, 65, false}});
        error(get_mempolicy(&p, m, 65, nullptr, 0), EINVAL);
        assert(get_mempolicy(&p, m, 66, nullptr, 4) == 0 && m[0] == 0 && m[1] == 2);
        for (auto bits : {32770UL, ULONG_MAX}) {
            error(get_mempolicy(nullptr, m, bits, nullptr, 0), EINVAL);
        }
        unsigned long page[513];
        for (auto& word : page) { word = ~0UL; }
        assert(get_mempolicy(nullptr, page, 32769, nullptr, 0) == 0);
        for (unsigned i = 0; i < 512; ++i) { assert(page[i] == 0); }
        assert(page[512] == ~0UL);
    } else if (!strcmp(test, "flags")) {
        for (int flags : {1, 5, 6, 7, 8, -1}) {
            error(get_mempolicy(&p, m, 65, nullptr, flags), EINVAL);
            assert(p == -77 && m[0] == ~0UL);
        }
        error(get_mempolicy(nullptr, nullptr, 0, nullptr, 1), EINVAL);
        error(get_mempolicy(&p, nullptr, 0, reinterpret_cast<void*>(1), 0), EINVAL);
        for (int flags : {2, 3}) {
            for (void* addr : {static_cast<void*>(nullptr), reinterpret_cast<void*>(1), static_cast<void*>(m)}) {
                error(get_mempolicy(&p, m, 65, addr, flags), EOPNOTSUPP);
                assert(p == -77 && m[0] == ~0UL);
            }
        }
    } else if (!strcmp(test, "reset")) {
        assert(set_mempolicy(0, nullptr, 0) == 0);
        assert(set_mempolicy(0, nullptr, ULONG_MAX) == 0);
        error(set_mempolicy(0, m, 0), EINVAL);
        assert(set_mempolicy(0, m, 1) == 0); // Effective zero bits.
        m[0] = 1;
        error(set_mempolicy(0, m, 65), EINVAL);
        m[0] = 0; m[1] = 2;
        assert(set_mempolicy(0, m, 66) == 0); // Bit65 outside effective65 bits.
        m[1] = 1;
        error(set_mempolicy(0, m, 66), EINVAL);
        m[1] = 0;
        assert(set_mempolicy(0, m, 66) == 0);
        m[0] = 1UL<<63;
        assert(set_mempolicy(0, m, 64) == 0);
        error(set_mempolicy(0, m, 65), EINVAL);
        error(set_mempolicy(0, m, ULONG_MAX), EINVAL);
        error(set_mempolicy(0, m, 32770), EINVAL);
    } else if (!strcmp(test, "high-padding")) {
        // Literal OSv contract, not a replica of Linux MAX_NUMNODES scanning.
        // Columns: empty mask, word16 bit0, bit1, bit63. See documented native
        // divergence at maxnode1026; 1024 is a fixture boundary, NOT an ABI cap.
        const unsigned long sizes[] = {1025, 1026, 1089, 1090};
        const int bits[] = {-1, 0, 1, 63};
        const int errors[4][4] = {{0,0,0,0}, {0,EINVAL,0,0},
                                 {0,EINVAL,EINVAL,EINVAL}, {0,EINVAL,EINVAL,EINVAL}};
        for (unsigned i = 0; i < 4; ++i) {
            for (unsigned j = 0; j < 4; ++j) {
                struct { unsigned long before, words[18], after; } mask{}, saved;
                mask.before = 0x123456789abcdef0UL;
                mask.after = 0xfedcba9876543210UL;
                if (bits[j] >= 0) { mask.words[16] = 1UL << bits[j]; }
                saved = mask;
                errno = 0;
                long rc = set_mempolicy(0, mask.words, sizes[i]);
                assert(rc == (errors[i][j] ? -1 : 0) && errno == errors[i][j]);
                assert(!memcmp(&mask, &saved, sizeof(mask)));
                printf("boundary maxnode=%lu word16bit=%d rc=%ld errno=%d\n",
                       sizes[i], bits[j], rc, errno);
            }
        }
    } else if (!strcmp(test, "placement")) {
        for (int mode : {1,2,3,4,5,6, 1<<14, 1<<15, 2|(1<<13), 5|(1<<13)}) {
            error(set_mempolicy(mode, nullptr, 0), EOPNOTSUPP);
        }
        for (int mode : {-1,7,8, 1<<12, (1<<14)|(1<<15), 1<<13}) {
            error(set_mempolicy(mode, nullptr, 0), EINVAL);
        }
    } else if (!strcmp(test, "cpu")) {
        topology(3, true, {{0x1000, 0x1000, 0, false}});
        numa::s_topology.cpu_to_node = {{7, 2}};
        sched::current_cpu.id = 7;
        unsigned cpu = 99, node = 99;
        assert(sys_getcpu(&cpu, &node, nullptr) == 0 && cpu == 7 && node == 2);
        assert(sys_getcpu(nullptr, &node, reinterpret_cast<void*>(1)) == 0 && node == 2);
        sched::current_cpu.id = 8;
        assert(sys_getcpu(&cpu, &node, nullptr) == 0 && cpu == 8 && node == 0);
        assert(!numa::cpu_node_known(cpu));
        assert(sys_getcpu(nullptr, nullptr, nullptr) == 0);
    } else if (!strcmp(test, "unaligned")) {
        char mask[24]; memset(mask, 0xff, sizeof(mask));
        char policy[8]; memset(policy, 0xff, sizeof(policy));
        assert(get_mempolicy(reinterpret_cast<int*>(policy+1),
               reinterpret_cast<unsigned long*>(mask+1), 65, nullptr, 4) == 0);
        unsigned long word; memcpy(&word, mask+1, sizeof(word));
        memcpy(&p, policy+1, sizeof(p));
        assert(word == 1 && p == 0 && mask[0] == char(0xff) && mask[9] == char(0xff));
        memset(mask+1, 0, 8);
        assert(set_mempolicy(0, reinterpret_cast<unsigned long*>(mask+1), 65) == 0);
#if HAVE_NODE_OF_PHYS
    } else if (!strcmp(test, "physical")) {
        assert(numa::node_of_phys(0) == -1);
        topology(3, true, {{0x1000, 0x1000, 2, false}, {0x4000, 0x1000, 1, false}});
        assert(numa::node_of_phys(0xfff) == -1);
        assert(numa::node_of_phys(0x1000) == 2);
        assert(numa::node_of_phys(0x1fff) == 2);
        assert(numa::node_of_phys(0x2000) == -1);
        assert(numa::node_of_phys(0x4000) == 1);
        assert(numa::node_of_phys(UINT64_MAX) == -1);
#endif
    } else { assert(false); }
    printf("PASS %s\n", test);
}
