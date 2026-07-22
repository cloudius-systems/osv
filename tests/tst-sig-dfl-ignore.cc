/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * SIGCHLD, SIGURG and SIGWINCH have a POSIX default disposition of "ignore".
 * Delivering one of them to a process that has installed no handler must be a
 * no-op, NOT fatal.  Before the fix, OSv's kill() treated any SIG_DFL signal as
 * uncaught-and-fatal and powered the VM off, so raising SIGCHLD with no handler
 * would kill the guest.  This test raises each of the three with the default
 * disposition and asserts the process simply survives.
 */

#include <signal.h>
#include <unistd.h>
#include <cstdio>
#include <cassert>
#include <initializer_list>

int main()
{
    printf("=== tst-sig-dfl-ignore ===\n");

    // No handlers installed => default disposition.  Per POSIX these three
    // default to ignore, so raise() must return 0 and we must keep running.
    for (int sig : {SIGCHLD, SIGURG, SIGWINCH}) {
        int r = raise(sig);
        printf("raise(%d) -> %d\n", sig, r);
        assert(r == 0);
    }

    // kill(getpid(), ...) of the same signals must also be a no-op survivor.
    for (int sig : {SIGCHLD, SIGURG, SIGWINCH}) {
        int r = kill(getpid(), sig);
        printf("kill(self, %d) -> %d\n", sig, r);
        assert(r == 0);
    }

    // If we reached here, the process was not terminated by any of them.
    printf("=== tst-sig-dfl-ignore: PASS (survived SIGCHLD/SIGURG/SIGWINCH) ===\n");
    return 0;
}
