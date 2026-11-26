/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Programs run one after another, each in a new ELF namespace and each joined
// and released before the next starts, must not run out of namespaces: a
// namespace whose program is gone is free again.  40 programs, more than
// there are namespaces.

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <osv/run.hh>

int main()
{
    std::vector<std::string> args{"/tests/payload-env.so"};
    int failures = 0;
    for (int i = 0; i < 40; i++) {
        int ret = -1;
        auto app = osv::run("/tests/payload-env.so", args, &ret, true);
        if (!app || ret != 0) {
            printf("FAIL: program %d: %s, return code %d\n", i,
                   app ? "ran" : "did not start", ret);
            failures++;
            break;
        }
    }
    if (!failures) {
        printf("PASS: tst-namespace-reuse: 40 programs in new namespaces\n");
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
