/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Trivial exec payload used by tst-fork / tst-execve: prints a marker so the
 * test can confirm the exec'd program actually ran, then exit(7)s so the
 * parent can verify the exit code was reaped.
 */
#include <cstdio>
int main() {
    printf("payload-exit7: running, will exit(7)\n");
    fflush(stdout);
    return 7;
}
