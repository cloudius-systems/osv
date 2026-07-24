/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for [W-dsm]: PostgreSQL's dynamic-shared-memory (DSM) POSIX
 * create/attach across fork.  PG's DSM POSIX backend
 * (src/backend/storage/ipc/dsm_impl.c dsm_impl_posix) CREATES a segment in one
 * process with shm_open(name, O_CREAT|O_RDWR|O_EXCL) + ftruncate + mmap
 * (MAP_SHARED), and ATTACHES it from ANOTHER (forked) process with
 * shm_open(name, O_RDWR) [no O_CREAT] + mmap(MAP_SHARED).  Every backend
 * attaches the DSM control segment during InitPostgres, so this must work for
 * the very first query, not just parallel query.
 *
 * OSv keeps POSIX named segments in a kernel-static registry
 * (libc/shm.cc: posix_shm_objects, name -> fileref).  The std::unordered_map
 * OBJECT lives in shared kernel BSS, but its NODES / bucket array / std::string
 * keys / fileref control blocks are heap-allocated -- and an app thread's
 * allocations land in the COW fork arena, so each process gets a PRIVATE,
 * divergent copy of the map contents.  A name inserted by the creating process
 * is then invisible to a forked attaching process -> shm_open(name, O_RDWR)
 * returns ENOENT.  That is exactly the wall PG hits:
 *   "could not open shared memory segment \"/PostgreSQL.<n>\": ENOENT".
 *
 * The fix forces the registry mutations (and shm_file::_pages, the physical
 * huge-page map) onto the identity kernel heap so all address spaces share ONE
 * registry and ONE set of backing pages -- the same rule already applied to
 * struct file, the signal waiters list, and thread objects.
 *
 * This test mirrors the DSM handoff directly:
 *   parent (process A) shm_open(O_CREAT|O_EXCL) + ftruncate + mmap(MAP_SHARED),
 *     writes a magic value;
 *   fork();
 *   the CHILD (process B) shm_open(same name, O_RDWR) + mmap(MAP_SHARED), and:
 *     (1) the attach must NOT return ENOENT (the ENOENT wall);
 *     (2) the value the parent wrote must be visible (create/attach coherence);
 *     (3) a value the child writes must become visible in the PARENT
 *         (MAP_SHARED write-back coherence, which PG relies on).
 * The CHILD owns the PASS/FAIL verdict (like a real PG backend), and reports
 * whether the parent could observe the child's write via a shared byte flag.
 *
 * To reproduce (in arena-dev, CONF_fork=y build):
 *   ./scripts/build conf_fork=1 fs=rofs image=tests, then boot with -smp 2
 *   (fork needs >=2 vCPUs on this build; -smp 1 hangs fork):
 *   qemu ... --rootfs=rofs /tests/tst-fork-posix-shm.so  (-smp 2)
 */

#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cstdint>

static const char *SHM_NAME = "/tst-fork-posix-shm.seg";
static const size_t SHM_SIZE = 1u << 20;    // 1 MiB
static const uint64_t PARENT_MAGIC = 0xC0FFEE1234567890ULL;
static const uint64_t CHILD_MAGIC  = 0xBADC0DE0FEEDFACEULL;

// Layout of the shared segment.
struct shm_layout {
    uint64_t parent_val;   // written by parent (creator) before fork
    uint64_t child_val;    // written by child (attacher) after attach
    volatile int child_wrote;   // child sets 1 after writing child_val
};

int main()
{
    // Unbuffered stdout: a fork child's exit does not flush the shared stdio
    // buffer on OSv, so make every printf visible immediately.
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== tst-fork-posix-shm ===\n");

    // Fresh start: remove a stale name if a prior run left one.
    shm_unlink(SHM_NAME);

    // Process A: CREATE the segment (O_CREAT|O_EXCL), size it, map it SHARED.
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_EXCL, 0600);
    if (fd < 0) {
        printf("FAIL: parent shm_open(O_CREAT|O_EXCL) errno=%d\n", errno);
        return 1;
    }
    if (ftruncate(fd, SHM_SIZE) < 0) {
        printf("FAIL: parent ftruncate errno=%d\n", errno);
        return 1;
    }
    void *pa = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pa == MAP_FAILED) {
        printf("FAIL: parent mmap(MAP_SHARED) errno=%d\n", errno);
        return 1;
    }
    // PG closes the create fd after mapping; the NAME persists (unlink only on
    // DESTROY).  Mirror that so we exercise attach-by-name, not fd inheritance.
    close(fd);

    struct shm_layout *A = (struct shm_layout *)pa;
    A->parent_val = PARENT_MAGIC;
    A->child_val = 0;
    A->child_wrote = 0;
    printf("parent created \"%s\", wrote parent_val=0x%llx\n",
           SHM_NAME, (unsigned long long)A->parent_val);

    pid_t pid = fork();
    if (pid == 0) {
        // Process B = the forked backend: ATTACH the segment BY NAME.  This is
        // the shm_open(name, O_RDWR) that returned ENOENT on HEAD.
        int cfd = shm_open(SHM_NAME, O_RDWR, 0600);
        if (cfd < 0) {
            printf("FAIL: forked backend shm_open(O_RDWR) errno=%d "
                   "(ENOENT=%d) -- DSM attach-by-name lost across fork\n",
                   errno, ENOENT);
            _exit(1);
        }
        void *pb = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE,
                        MAP_SHARED, cfd, 0);
        if (pb == MAP_FAILED) {
            printf("FAIL: forked backend mmap(MAP_SHARED) errno=%d\n", errno);
            _exit(1);
        }
        struct shm_layout *B = (struct shm_layout *)pb;

        // (2) The parent's pre-fork write must be visible in the attached seg.
        if (B->parent_val != PARENT_MAGIC) {
            printf("FAIL: forked backend read parent_val=0x%llx, expected "
                   "0x%llx -- create/attach not coherent\n",
                   (unsigned long long)B->parent_val,
                   (unsigned long long)PARENT_MAGIC);
            _exit(1);
        }

        // (3) A write in B must be visible in A (MAP_SHARED write-back).
        B->child_val = CHILD_MAGIC;
        __sync_synchronize();
        B->child_wrote = 1;
        __sync_synchronize();

        printf("PASS: forked backend attached by name (no ENOENT), read "
               "parent_val=0x%llx, wrote child_val=0x%llx at %p\n",
               (unsigned long long)B->parent_val,
               (unsigned long long)B->child_val, (void*)B);
        _exit(0);
    }
    if (pid < 0) {
        printf("FAIL: fork() errno=%d\n", errno);
        return 1;
    }

    // Parent waits (poll the shared flag) for the child's write to land, then
    // confirms MAP_SHARED write-back coherence from the child.
    for (int i = 0; i < 300 && !A->child_wrote; i++) {
        usleep(10000);
        __sync_synchronize();
    }
    printf("parent: after wait child_wrote=%d child_val=0x%llx (A=%p)\n",
           A->child_wrote, (unsigned long long)A->child_val, (void*)A);
    if (!A->child_wrote) {
        printf("FAIL: child did not write child_val within timeout "
               "(backend attach failed -- see child FAIL above)\n");
        shm_unlink(SHM_NAME);
        return 1;
    }
    if (A->child_val != CHILD_MAGIC) {
        printf("FAIL: parent observed child_val=0x%llx, expected 0x%llx -- "
               "MAP_SHARED write from child not visible in parent\n",
               (unsigned long long)A->child_val,
               (unsigned long long)CHILD_MAGIC);
        shm_unlink(SHM_NAME);
        return 1;
    }
    printf("PASS: parent observed child's MAP_SHARED write child_val=0x%llx "
           "-- POSIX shm coherent across fork\n",
           (unsigned long long)A->child_val);

    shm_unlink(SHM_NAME);
    return 0;
}
