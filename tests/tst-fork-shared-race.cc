/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for the post-fork MAP_SHARED write-visibility race (fork
 * WALL 1, see /tmp/pg-concurrent-fix.txt).
 *
 * A child that ATTACHES a MAP_SHARED POSIX shm segment AFTER fork() installs a
 * fresh mapping into its OWN (child) address space.  The physical huge page is
 * shared (shm_file::_pages is identity-heap), so parent and child map the SAME
 * physical frame -- gdb proved this.  Yet under normal SMP timing the parent
 * never observes a value the child writes into that shared page: it PASSES only
 * when gdb single-stepping serializes the two CPUs.  Signature of a cross-CPU
 * visibility problem -- a stale PTE / missing TLB shootdown on the newly
 * installed post-fork MAP_SHARED mapping, or a spurious COW-privatize turning
 * the shared frame private on one side.
 *
 * This test drives exactly that hazard with the two processes pinned to run
 * concurrently (no serialization):
 *   parent (AS0) shm_open(O_CREAT|O_EXCL)+ftruncate+mmap(MAP_SHARED), writes a
 *     seed value, then fork()s;
 *   the child (its own AS) shm_open(same name, O_RDWR)+mmap(MAP_SHARED) -- a
 *     mapping installed POST-fork -- verifies the seed, then repeatedly writes
 *     a monotonically increasing counter into the shared page;
 *   the parent BUSY-POLLS the counter and must observe it advance within a
 *     bounded time.  A stale-PTE / missing-shootdown bug leaves the parent
 *     reading its own stale copy forever (or a fixed value), so the parent
 *     times out -> FAIL.  The child owns the seed-visibility verdict; the
 *     parent owns the write-visibility verdict.
 *
 * Must reproduce WITHOUT gdb serialization: run under -smp 2 (and -smp 4).
 * Rounds are large enough that the parent and child genuinely run on two CPUs.
 *
 * To reproduce on HEAD (fix reverted): the parent times out and prints
 *   FAIL: parent never observed child's advancing MAP_SHARED writes ...
 * With the fix the parent observes the child's counter climb and both agree.
 */

#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <ctime>

static const char *SHM_NAME = "/tst-fork-shared-race.seg";
static const size_t SHM_SIZE = 1u << 20;    // 1 MiB
static const uint64_t SEED_MAGIC = 0x5EED5EED5EED5EEDULL;

struct shm_layout {
    volatile uint64_t seed;      // parent writes before fork
    volatile uint64_t counter;   // child bumps repeatedly post-fork
    volatile int child_done;     // child sets when finished
};

// Enough writes that the child stays busy for well over the parent's poll
// budget, so the two run concurrently on separate CPUs (no serialization).
static const uint64_t ROUNDS = 2000000ULL;

static double now_sec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== tst-fork-shared-race ===\n");

    shm_unlink(SHM_NAME);

    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0) { printf("FAIL: parent shm_open errno=%d\n", errno); return 1; }
    if (ftruncate(fd, SHM_SIZE) < 0) { printf("FAIL: ftruncate errno=%d\n", errno); return 1; }
    void *pa = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pa == MAP_FAILED) { printf("FAIL: parent mmap errno=%d\n", errno); return 1; }
    close(fd);

    struct shm_layout *A = (struct shm_layout *)pa;
    A->seed = SEED_MAGIC;
    A->counter = 0;
    A->child_done = 0;
    __sync_synchronize();

    pid_t pid = fork();
    if (pid == 0) {
        // Child: attach POST-fork, verify seed, then bump the shared counter.
        int cfd = shm_open(SHM_NAME, O_RDWR, 0600);
        if (cfd < 0) { printf("FAIL: child shm_open errno=%d\n", errno); _exit(1); }
        void *pb = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, cfd, 0);
        if (pb == MAP_FAILED) { printf("FAIL: child mmap errno=%d\n", errno); _exit(2); }
        struct shm_layout *B = (struct shm_layout *)pb;
        if (B->seed != SEED_MAGIC) {
            printf("FAIL: child read seed=0x%llx expected 0x%llx (attach not coherent)\n",
                   (unsigned long long)B->seed, (unsigned long long)SEED_MAGIC);
            _exit(3);
        }
        for (uint64_t i = 1; i <= ROUNDS; i++) {
            B->counter = i;
            __sync_synchronize();
        }
        B->child_done = 1;
        __sync_synchronize();
        printf("child: wrote counter up to %llu into post-fork MAP_SHARED page\n",
               (unsigned long long)ROUNDS);
        _exit(0);
    }
    if (pid < 0) { printf("FAIL: fork errno=%d\n", errno); return 1; }

    // Parent: busy-poll the shared counter.  It MUST advance (child writes are
    // visible through the shared physical page) within a bounded time.  A
    // stale-PTE / missing-shootdown bug leaves it stuck reading a stale value.
    uint64_t first_seen = A->counter;
    uint64_t last = first_seen;
    bool advanced = false;
    double t0 = now_sec();
    while (now_sec() - t0 < 5.0) {
        __sync_synchronize();
        uint64_t c = A->counter;
        if (c != last) {
            if (c > first_seen) advanced = true;
            last = c;
        }
        if (A->child_done && A->counter >= 1) { advanced = advanced || (A->counter > 0); break; }
    }
    __sync_synchronize();
    printf("parent: observed counter first=%llu last=%llu child_done=%d\n",
           (unsigned long long)first_seen, (unsigned long long)last, A->child_done);

    int status = 0;
    waitpid(pid, &status, 0);
    int cec = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    int failures = 0;
    if (cec != 0) {
        printf("FAIL: child exit code %d (seed/attach failed -- see child FAIL)\n", cec);
        failures++;
    }
    if (!advanced) {
        printf("FAIL: parent never observed child's advancing MAP_SHARED writes "
               "(stale PTE / missing TLB shootdown on post-fork shared mapping)\n");
        failures++;
    } else {
        printf("PASS: parent observed child's post-fork MAP_SHARED writes advance "
               "(last=%llu) -- cross-AS shared page coherent without serialization\n",
               (unsigned long long)last);
    }

    shm_unlink(SHM_NAME);
    printf("=== tst-fork-shared-race done: %d failures ===\n", failures);
    return failures == 0 ? 0 : 1;
}
