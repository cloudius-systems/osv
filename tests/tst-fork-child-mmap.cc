/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for [W-mmap]: a fork child that calls mmap() after fork()
 * and writes the region.  Before the AS-aware mmu allocation refactor this
 * SIGSEGV'd and aborted the whole unikernel with "page fault outside
 * application, addr: 0x2000002XXXXX"; it now PASSES.
 *
 * Root cause: the per-child address space was
 * consulted ONLY by the page-FAULT path -- core/mmu.cc vm_fault() looks up
 * `as->vmas` (the child's private vma_list, populated by clone_address_space).
 * The mmap/munmap/mprotect ALLOCATION path (mmu::allocate / map_anon / find_hole
 * / evacuate, plus the global vma_range_set) was NOT address-space aware: it
 * always operated on the GLOBAL vma_list.  So a fork child's new mmap:
 *   1. picked a hole from the GLOBAL vma_list (often colliding with the child's
 *      own same-VA stack top, e.g. 0x200000201000), and
 *   2. inserted the new vma into the GLOBAL vma_list, NOT the child's as->vmas.
 * The child's first write then #PF'd, vm_fault searched as->vmas, found no vma,
 * and SIGSEGV'd.  Because the write is done by the kernel's rep-stos memset
 * (a KERNEL pc), vm_sigsegv() called abort() and the unikernel went down.
 *
 * The fix routes the allocation path through the current thread's
 * address_space (per-AS vma_list + per-AS vma_range_set) via cur_vma_list() /
 * cur_vma_range_set(), defaulting to AS0 (the global lists) so the non-fork
 * path is byte-identical.  Deterministic on -smp 1 with a single mmap+write.
 *
 * To reproduce (in arena-dev, CONF_fork=y build):
 *   ./scripts/build conf_fork=1 fs=rofs image=tests, then:
 *   ./scripts/run.py -c 1 -e /tests/tst-fork-child-mmap.so
 */

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstring>
#include <cerrno>

int main()
{
    // fork() is opt-in (CONFIG_fork). When it is off it returns -1/ENOSYS;
    // skip cleanly, as tst-fork does, so the default test suite stays green.
    {
        errno = 0;
        pid_t probe = fork();
        if (probe == -1 && errno == ENOSYS) {
            printf("SKIP: fork() not enabled (CONFIG_fork off)\n");
            return 0;
        }
        if (probe == 0) {
            _exit(0);   // the probe fork's child: exit immediately
        }
        if (probe > 0) {
            int st = 0;
            waitpid(probe, &st, 0);   // reap the probe child
        }
    }
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
