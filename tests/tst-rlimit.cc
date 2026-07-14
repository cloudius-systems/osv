/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises setrlimit/getrlimit/prlimit consistency.  getrlimit() used to
// abort() on any resource it did not special-case, and setrlimit() was a
// no-op inconsistent with getrlimit(); both are fixed here.  Built and run as
// part of the OSv test image.

#include <sys/resource.h>

#include <errno.h>
#include <unistd.h>

#include <cassert>
#include <iostream>

int main()
{
    std::cerr << "Running rlimit tests\n";
    struct rlimit rl;

    // Every resource in range must be queryable without aborting.
    for (int r = 0; r < RLIMIT_NLIMITS; r++) {
        assert(getrlimit(r, &rl) == 0);
    }

    // Out-of-range resource yields EINVAL, not a crash.
    errno = 0;
    assert(getrlimit(-1, &rl) == -1 && errno == EINVAL);
    errno = 0;
    assert(getrlimit(RLIMIT_NLIMITS, &rl) == -1 && errno == EINVAL);

    // set/get round-trip.
    struct rlimit set { 4096, 8192 };
    assert(setrlimit(RLIMIT_NOFILE, &set) == 0);
    assert(getrlimit(RLIMIT_NOFILE, &rl) == 0);
    assert(rl.rlim_cur == 4096 && rl.rlim_max == 8192);

    // Previously an abort() trigger: RLIMIT_SIGPENDING is now handled.
    struct rlimit sp { 100, 200 };
    assert(setrlimit(RLIMIT_SIGPENDING, &sp) == 0);
    assert(getrlimit(RLIMIT_SIGPENDING, &rl) == 0);
    assert(rl.rlim_cur == 100 && rl.rlim_max == 200);

    // rlim_cur > rlim_max is rejected.
    struct rlimit bad { 200, 100 };
    errno = 0;
    assert(setrlimit(RLIMIT_NOFILE, &bad) == -1 && errno == EINVAL);

    // prlimit for our own pid gets/sets consistently.
    struct rlimit newl { 2048, 4096 }, old;
    assert(prlimit(0, RLIMIT_NOFILE, &newl, &old) == 0);
    assert(old.rlim_cur == 4096 && old.rlim_max == 8192);  // prior value
    assert(getrlimit(RLIMIT_NOFILE, &rl) == 0);
    assert(rl.rlim_cur == 2048 && rl.rlim_max == 4096);

    std::cerr << "rlimit tests PASSED\n";
    return 0;
}
