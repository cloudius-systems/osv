/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * STANDALONE REPRO (deliberately NOT registered in modules/tests/Makefile):
 * a fork child that calls mmap() after fork() and writes the region.  On the
 * integ/pg-fork-arena branch this SIGSEGVs and aborts the whole unikernel with
 * "page fault outside application, addr: 0x2000002XXXXX".
 *
 * Root cause (proven, see /tmp/pg-preempt-fix.txt): the per-child address space
 * is consulted ONLY by the page-FAULT path -- core/mmu.cc vm_fault() looks up
 * `as->vmas` (the child's private vma_list, populated by clone_address_space).
 * The mmap/munmap/mprotect ALLOCATION path (mmu::allocate / map_anon / find_hole
 * / evacuate, plus the global vma_range_set) is NOT address-space aware: it
 * always operates on the GLOBAL vma_list.  So a fork child's new mmap:
 *   1. picks a hole from the GLOBAL vma_list (often colliding with the child's
 *      own same-VA stack top, e.g. 0x200000201000), and
 *   2. inserts the new vma into the GLOBAL vma_list, NOT the child's as->vmas.
 * The child's first write then #PFs, vm_fault searches as->vmas, finds no vma,
 * and SIGSEGVs.  Because the write is done by the kernel's rep-stos memset
 * (a KERNEL pc), vm_sigsegv() calls abort() and the unikernel goes down.
 *
 * Deterministic on -smp 1 with a single mmap+write -- NOT preemption dependent.
 *
 * To reproduce (in arena-dev, CONF_fork=y build):
 *   g++ ... -o tst-fork-child-mmap.so tests/tst-fork-child-mmap.cc   (or add to
 *   the tests Makefile temporarily), then:
 *   ./scripts/run.py -c 1 -e /tests/tst-fork-child-mmap.so
 *
 * A correct fix makes mmu::allocate/map_anon/find_hole/evacuate/munmap/mprotect
 * operate on the current thread's address_space (per-AS vma_list + a per-AS
 * vma_range_set), defaulting to the kernel AS so the non-fork path is unchanged.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstring>

int main()
{
    printf("=== tst-fork-child-mmap ===\n");
    pid_t pid = fork();
    if (pid == 0) {
        void *p = mmap(nullptr, 65536, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        printf("child mmap = %p\n", p);
        if (p == MAP_FAILED) {
            _exit(70);
        }
        // BUG: this first write SIGSEGVs (page fault outside application) because
        // the mapping went into the global vma_list, invisible to the child's
        // per-AS fault handler.  On a correct implementation this returns 0.
        memset(p, 0x5a, 65536);
        printf("child memset OK (mmap-in-child works)\n");
        munmap(p, 65536);
        _exit(55);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 55) {
        printf("PASS: fork child mmap()+write works\n");
        return 0;
    }
    printf("FAIL/KNOWN-LIMITATION: fork child mmap()+write did not succeed "
           "(status=0x%x)\n", status);
    return 1;
}
