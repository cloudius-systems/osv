/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// setsid() must succeed on OSv and return the caller's pid as the session id:
// servers such as PostgreSQL treat a failing setsid() as fatal.

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
    printf("PASS: setsid() returned the caller's pid\n");
    return 0;
}
