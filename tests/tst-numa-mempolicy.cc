/* Copyright (C) 2026 Greg Burd. BSD license, see LICENSE. */
// Runtime dispatcher checks, not page placement or allocator locality tests.
#include <osv/numa.hh>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstdio>

static long query(int* mode, unsigned long* mask, unsigned long maxnode,
                  void* addr, int flags)
{
    return syscall(SYS_get_mempolicy, mode, mask, maxnode, addr, flags);
}

int main(int argc, char** argv)
{
    printf("Running numa query syscall tests\n");
    if (argc > 1) { assert(numa::nr_nodes() == strtoul(argv[1], nullptr, 0)); }
    if (argc > 2) { assert(numa::available() == bool(strtoul(argv[2], nullptr, 0))); }
    unsigned long expected = argc > 3 ? strtoul(argv[3], nullptr, 0) : 0;
    unsigned long mask[5] = {~0UL, ~0UL, ~0UL, ~0UL, ~0UL};
    int mode = -1;
    assert(query(&mode, mask, 257, nullptr, 0) == 0 && mode == 0);
    for (unsigned i = 0; i < 4; ++i) { assert(mask[i] == 0); }
    assert(mask[4] == ~0UL);
    assert(query(&mode, mask, 257, nullptr, 4) == 0 && mode == 0);
    if (argc > 3) { assert(mask[0] == expected); }
    for (unsigned i = 1; i < 4; ++i) { assert(mask[i] == 0); }
    assert(mask[4] == ~0UL);
    assert(query(&mode, mask, 0, nullptr, 4) == -1 && errno == EINVAL);
    assert(query(nullptr, nullptr, ULONG_MAX, nullptr, 0) == 0);
    for (int flags : {1, 5, 6, 7, 8, -1}) {
        assert(query(nullptr, nullptr, 0, nullptr, flags) == -1 && errno == EINVAL);
    }
    for (int flags : {2, 3}) {
        assert(query(&mode, nullptr, 0, reinterpret_cast<void*>(1), flags) == -1);
        assert(errno == EOPNOTSUPP);
    }
    assert(syscall(SYS_set_mempolicy, 0, nullptr, 0) == 0);
    unsigned long empty = 0, nonempty = 1;
    assert(syscall(SYS_set_mempolicy, 0, &empty, 65) == 0);
    assert(syscall(SYS_set_mempolicy, 0, &nonempty, 65) == -1 && errno == EINVAL);
    assert(syscall(SYS_set_mempolicy, 2, &nonempty, 65) == -1 && errno == EOPNOTSUPP);
    assert(query(&mode, mask, 257, nullptr, 0) == 0 && mode == 0 && mask[0] == 0);

    cpu_set_t saved;
    assert(sched_getaffinity(0, sizeof(saved), &saved) == 0);
    for (unsigned id = 0; id < sizeof(saved) * CHAR_BIT; ++id) {
        if (!CPU_ISSET(id, &saved)) { continue; }
        cpu_set_t one; CPU_ZERO(&one); CPU_SET(id, &one);
        assert(sched_setaffinity(0, sizeof(one), &one) == 0);
        unsigned cpu = ~0u, node = ~0u;
        assert(syscall(SYS_getcpu, &cpu, &node, nullptr) == 0);
        assert(cpu == id && node == numa::node_of_cpu(id));
        printf("query cpu=%u node=%u known=%d\n", cpu, node, numa::cpu_node_known(cpu));
    }
    assert(sched_setaffinity(0, sizeof(saved), &saved) == 0);
    for (const auto& range : numa::memory_ranges()) {
        assert(numa::node_of_phys(range.base) == int(range.node));
        assert(numa::node_of_phys(range.base + range.length - 1) == int(range.node));
    }
    assert(numa::node_of_phys(UINT64_MAX) == -1);
    printf("numa query syscall tests PASSED nodes=%u available=%d\n",
           numa::nr_nodes(), numa::available());
}
