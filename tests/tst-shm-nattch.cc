/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// shmctl(IPC_STAT) must report the number of current attachments of a
// segment in shm_nattch, as each shmat() and shmdt() changes it, and the
// segment's key, mode, size, creator and last-operation pids, and times, as
// Linux does.

#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failures;

static void check(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
}

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

static void *attach(int id)
{
    void *p = shmat(id, nullptr, 0);
    if (p == (void *)-1) {
        printf("FAIL: shmat: %s\n", strerror(errno));
        failures++;
    }
    return p;
}

// The fields other than shm_nattch, for a keyed segment through its life.
static void check_fields()
{
    const key_t key = 0x15790001;
    time_t t0 = time(nullptr);
    int id = shmget(key, 1000, IPC_CREAT | IPC_EXCL | 0640);
    if (id < 0) {
        printf("FAIL: shmget: %s\n", strerror(errno));
        failures++;
        return;
    }
    struct shmid_ds ds;
    memset(&ds, 0xff, sizeof ds);
    check(shmctl(id, IPC_STAT, &ds) == 0, "IPC_STAT of a new segment succeeds");
    check(ds.shm_perm.__key == key, "shm_perm.__key is the key");
    check((ds.shm_perm.mode & 0777) == 0640, "shm_perm.mode is the mode passed to shmget()");
    check(ds.shm_segsz == 1000, "shm_segsz is the size passed to shmget()");
    check(ds.shm_cpid == getpid(), "shm_cpid is the creator's pid");
    check(ds.shm_lpid == 0, "shm_lpid is 0 before any shmat()");
    check(ds.shm_ctime >= t0, "shm_ctime is set");
    check(ds.shm_atime == 0 && ds.shm_dtime == 0, "shm_atime and shm_dtime are 0 before any shmat()");

    void *p = attach(id);
    memset(&ds, 0xff, sizeof ds);
    shmctl(id, IPC_STAT, &ds);
    check(ds.shm_atime >= t0 && ds.shm_dtime == 0, "shmat() sets shm_atime only");
    check(ds.shm_lpid == getpid(), "shmat() sets shm_lpid");
    check(ds.shm_segsz == 1000, "shm_segsz is unchanged by shmat()");
    if (p != (void *)-1) {
        shmdt(p);
    }
    memset(&ds, 0xff, sizeof ds);
    shmctl(id, IPC_STAT, &ds);
    check(ds.shm_dtime >= t0, "shmdt() sets shm_dtime");

    // Another segment's fields are its own.
    int other = shmget(IPC_PRIVATE, 3 * 4096, IPC_CREAT | 0600);
    memset(&ds, 0xff, sizeof ds);
    check(other >= 0 && shmctl(other, IPC_STAT, &ds) == 0, "IPC_STAT of an IPC_PRIVATE segment succeeds");
    check(ds.shm_perm.__key == IPC_PRIVATE && (ds.shm_perm.mode & 0777) == 0600 &&
          ds.shm_segsz == 3 * 4096, "an IPC_PRIVATE segment reports its own key, mode and size");
    shmctl(other, IPC_RMID, nullptr);
    shmctl(id, IPC_RMID, nullptr);
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
    void *a = attach(id);
    expect_nattch(id, 1, "after one shmat");
    void *b = attach(id);
    expect_nattch(id, 2, "after two shmats");
    void *o = attach(other);
    expect_nattch(id, 2, "after attaching a different segment");
    expect_nattch(other, 1, "of the other segment");
    if (a != (void *)-1) {
        shmdt(a);
    }
    expect_nattch(id, 1, "after one shmdt");
    if (b != (void *)-1) {
        shmdt(b);
    }
    expect_nattch(id, 0, "after both shmdts");
    if (o != (void *)-1) {
        shmdt(o);
    }
    shmctl(id, IPC_RMID, nullptr);
    shmctl(other, IPC_RMID, nullptr);
    check_fields();
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
