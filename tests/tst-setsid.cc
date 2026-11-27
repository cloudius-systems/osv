/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// setsid() must succeed on OSv and return the caller's pid as the session id:
// servers such as PostgreSQL treat a failing setsid() as fatal.  Afterwards,
// as on Linux, the caller leads its session and process group, so getsid(),
// getpgrp() and getpgid() return its pid, and signalling that group with
// killpg(getpgrp(), sig) or kill(-getpgrp(), sig) reaches the caller.

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <atomic>

static int failures;
static std::atomic<int> usr1_count;

static void on_usr1(int)
{
    usr1_count++;
}

// OSv runs a signal handler on a new thread, so wait for it, up to 5 s.
static int wait_usr1(int want)
{
    for (int i = 0; i < 500 && usr1_count < want; i++) {
        usleep(10000);
    }
    return usr1_count;
}

static void expect(const char *what, pid_t got, pid_t want)
{
    printf("%s = %d (want %d)\n", what, (int)got, (int)want);
    if (got != want) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main()
{
    errno = 0;
    pid_t sid = setsid();
    int err = errno;
    pid_t pid = getpid();
    printf("setsid() = %d (errno %d: %s), getpid() = %d\n", (int)sid, err, strerror(err), (int)pid);
    if (sid != pid) {
        printf("FAIL: setsid() did not return the caller's pid\n");
        return 1;
    }
    expect("getsid(0)", getsid(0), pid);
    expect("getsid(getpid())", getsid(pid), pid);
    expect("getpgrp()", getpgrp(), pid);
    expect("getpgid(0)", getpgid(0), pid);
    expect("getpgid(getpid())", getpgid(pid), pid);

    // A pid that names no process.
    const pid_t none = 0x7ffffff0;
    errno = 0;
    expect("getsid(no such pid)", getsid(none), -1);
    expect("its errno", errno, ESRCH);
    errno = 0;
    expect("getpgid(no such pid)", getpgid(none), -1);
    expect("its errno", errno, ESRCH);

    // The caller's own process group, by the id getpgrp() reports.
    expect("killpg(getpgrp(), 0)", killpg(getpgrp(), 0), 0);
    expect("kill(-getpgrp(), 0)", kill(-getpgrp(), 0), 0);
    signal(SIGUSR1, on_usr1);
    expect("killpg(getpgrp(), SIGUSR1)", killpg(getpgrp(), SIGUSR1), 0);
    expect("handler runs after killpg()", wait_usr1(1), 1);
    expect("kill(-getpgrp(), SIGUSR1)", kill(-getpgrp(), SIGUSR1), 0);
    expect("handler runs after kill(-pgrp)", wait_usr1(2), 2);
    errno = 0;
    expect("kill(-(no such group), 0)", kill(-none, 0), -1);
    expect("its errno", errno, ESRCH);

    if (failures) {
        return 1;
    }
    printf("PASS: setsid() returned the caller's pid, which leads its session and group\n");
    return 0;
}
