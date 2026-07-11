/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Exercises the expanded prctl(2): PR_SET_NAME/PR_GET_NAME (real thread names)
// and the no-op options that used to return EINVAL.  Built and run as part of
// the OSv test image.

#include <sys/prctl.h>

#include <errno.h>
#include <string.h>

#include <cassert>
#include <iostream>

int main()
{
    std::cerr << "Running prctl tests\n";

    // PR_SET_NAME then PR_GET_NAME must round-trip (Linux caps at 16 bytes).
    assert(prctl(PR_SET_NAME, "worker-1") == 0);
    char name[16] = {};
    assert(prctl(PR_GET_NAME, name) == 0);
    assert(strcmp(name, "worker-1") == 0);

    // A name longer than 15 chars is truncated, not rejected.
    assert(prctl(PR_SET_NAME, "this-name-is-way-too-long") == 0);
    assert(prctl(PR_GET_NAME, name) == 0);
    assert(strlen(name) == 15);
    assert(strncmp(name, "this-name-is-wa", 15) == 0);

    // Dumpable: SET accepted, GET reports the Linux default (1).
    assert(prctl(PR_SET_DUMPABLE, 0) == 0);
    assert(prctl(PR_GET_DUMPABLE) == 1);

    // Benign no-op options are accepted rather than returning EINVAL.
    assert(prctl(PR_SET_PDEATHSIG, 9) == 0);
    int sig = -1;
    assert(prctl(PR_GET_PDEATHSIG, &sig) == 0);
    assert(sig == 0);
    assert(prctl(PR_SET_KEEPCAPS, 1) == 0);
    assert(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0);

    // An unknown option still returns EINVAL.
    errno = 0;
    assert(prctl(0x7fffffff) == -1 && errno == EINVAL);

    std::cerr << "prctl tests PASSED\n";
    return 0;
}
