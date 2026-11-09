/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A SysV shmid is a file descriptor on OSv. shmat() and shmctl() must reject
// one that names some other open file, instead of mapping it or closing it.

#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

int main()
{
    const char *path = "/tmp/tst-shm-stale-id";
    int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        printf("FAIL: open %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (write(fd, "x", 1) != 1) {
        printf("FAIL: write: %s\n", strerror(errno));
        return 1;
    }

    // An ordinary file's fd is not a shmid.
    errno = 0;
    void *p = shmat(fd, nullptr, 0);
    check(p == (void *)-1 && errno == EINVAL, "shmat() on a regular file's fd fails with EINVAL");
    if (p != (void *)-1) {
        shmdt(p);
    }

    struct shmid_ds ds;
    errno = 0;
    check(shmctl(fd, IPC_STAT, &ds) == -1 && errno == EINVAL,
          "shmctl(IPC_STAT) on a regular file's fd fails with EINVAL");

    errno = 0;
    check(shmctl(fd, IPC_RMID, nullptr) == -1 && errno == EINVAL,
          "shmctl(IPC_RMID) on a regular file's fd fails with EINVAL");

    // The rejected calls must not have closed the caller's file.
    check(fcntl(fd, F_GETFD) != -1, "the regular file's fd is still open afterwards");

    // A closed fd is not a shmid either.
    int closed = dup(fd);
    close(closed);
    errno = 0;
    check(shmctl(closed, IPC_RMID, nullptr) == -1 && errno == EINVAL,
          "shmctl(IPC_RMID) on a closed fd fails with EINVAL");

    // A real segment still works.
    int id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
    check(id >= 0, "shmget(IPC_PRIVATE) succeeds");
    void *a = shmat(id, nullptr, 0);
    check(a != (void *)-1, "shmat() on a real segment succeeds");
    check(shmctl(id, IPC_STAT, &ds) == 0, "shmctl(IPC_STAT) on a real segment succeeds");
    check(a != (void *)-1 && shmdt(a) == 0, "shmdt() succeeds");
    check(shmctl(id, IPC_RMID, nullptr) == 0, "shmctl(IPC_RMID) on a real segment succeeds");

    close(fd);
    unlink(path);
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
