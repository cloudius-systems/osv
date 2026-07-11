/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises the membarrier(2) syscall implemented in runtime.cc.
// Built and run as part of the OSv test image.

#include <sys/membarrier.h>

#include <errno.h>

#include <cassert>
#include <iostream>

static void test_membarrier()
{
    // QUERY reports a nonzero mask of supported commands.
    int supported = membarrier(MEMBARRIER_CMD_QUERY, 0, 0);
    assert(supported > 0);
    assert(supported & MEMBARRIER_CMD_GLOBAL);
    assert(supported & MEMBARRIER_CMD_PRIVATE_EXPEDITED);

    // Registration is accepted.
    assert(membarrier(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0, 0) == 0);
    assert(membarrier(MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED, 0, 0) == 0);

    // The actual barriers succeed (these broadcast an IPI to all other CPUs).
    assert(membarrier(MEMBARRIER_CMD_GLOBAL, 0, 0) == 0);
    assert(membarrier(MEMBARRIER_CMD_GLOBAL_EXPEDITED, 0, 0) == 0);
    assert(membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0) == 0);

    // Unknown command and nonzero flags are rejected with EINVAL.
    errno = 0;
    assert(membarrier(0x40000000, 0, 0) == -1 && errno == EINVAL);
    errno = 0;
    assert(membarrier(MEMBARRIER_CMD_GLOBAL, 0x1, 0) == -1 && errno == EINVAL);
}

int main()
{
    std::cerr << "Running membarrier tests\n";
    test_membarrier();
    std::cerr << "membarrier tests PASSED\n";
    return 0;
}
