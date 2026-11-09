/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// shmctl(IPC_STAT) must report the number of current attachments of a
// segment in shm_nattch, as each shmat() and shmdt() changes it.

#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void expect_nattch(int id, shmatt_t want, const char *when)
{
    struct shmid_ds ds;
    memset(&ds, 0xff, sizeof ds);
    int r = shmctl(id, IPC_STAT, &ds);
    bool ok = r == 0 && ds.shm_nattch == want;
    printf("%s: shm_nattch %s is %lu (want %lu)\n", ok ? "PASS" : "FAIL", when,
           r == 0 ? (unsigned long)ds.shm_nattch : 0UL, (unsigned long)want);
    failures += !ok;
}

int main()
{
    int id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
    int other = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
    if (id < 0 || other < 0) {
        printf("FAIL: shmget: %s\n", strerror(errno));
        return 1;
    }
    expect_nattch(id, 0, "before any shmat");
    void *a = shmat(id, nullptr, 0);
    expect_nattch(id, 1, "after one shmat");
    void *b = shmat(id, nullptr, 0);
    expect_nattch(id, 2, "after two shmats");
    void *o = shmat(other, nullptr, 0);
    expect_nattch(id, 2, "after attaching a different segment");
    expect_nattch(other, 1, "of the other segment");
    shmdt(a);
    expect_nattch(id, 1, "after one shmdt");
    shmdt(b);
    expect_nattch(id, 0, "after both shmdts");
    shmdt(o);
    shmctl(id, IPC_RMID, nullptr);
    shmctl(other, IPC_RMID, nullptr);
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
