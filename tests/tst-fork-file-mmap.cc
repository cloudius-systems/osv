/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for [W-branch]: a fork child that demand-faults a page of a
 * FILE-backed (MAP_PRIVATE) mapping which the PARENT never touched before the
 * fork.  Before the clone_address_space type-preservation fix this returned
 * ZEROS (the bug): clone_address_space rebuilt EVERY child vma as an anon_vma,
 * so a file_vma (e.g. PostgreSQL's file-backed .text, mapped by the ELF loader)
 * became anonymous in the child and its demand fault hit the base zero-fill
 * path instead of file_vma::fault reading the file.  It now PASSES (the child
 * reads the real file bytes).
 *
 * The mapping is of a READ-ONLY ROFS file -- matching the real case (an
 * executable's file-backed .text, demand-paged from the read-only image), and
 * avoiding the writable-fs access-time update that would otherwise write to the
 * inode during a read fault.
 *
 * Root cause (proven via KVM+hbreak, see /tmp/w-branch-kvm.txt):
 *   core/mmu.cc clone_address_space():
 *     struct vma_snap { start, end, perm, flags; };     // dropped dynamic type
 *     snap.push_back({v.start(), v.end(), v.perm(), v.flags()});
 *     auto *nv = new anon_vma(...);                      // ALWAYS anon_vma
 *   The child's file-backed VA thus faulted to a fresh ZERO anon page.  The
 *   existing tst-fork* tests only exercise ANON memory, so they miss it; PG is
 *   the first fork whose child demand-faults a file-backed .text page the
 *   parent never resident-loaded.
 *
 * Mechanism the test recreates:
 *   - parent opens a ROFS file and reads the EXPECTED bytes of a mid-file page
 *     via pread(2) on a SEPARATE fd (this never faults the mapping's PTE);
 *   - parent mmaps the file MAP_PRIVATE but does NOT read the target page (so
 *     no PTE for it exists in the parent -> the COW page-table clone hands the
 *     child an absent PTE);
 *   - fork();
 *   - the CHILD reads the target page FIRST -> demand fault -> must read the
 *     real file bytes.  On buggy HEAD it reads zeros (MISMATCH).
 *
 * Deterministic on -smp 1.  Gated CONF_fork.
 *
 * To reproduce (in arena-dev, CONF_fork=y build):
 *   ./scripts/build conf_fork=1 fs=rofs image=tests, then:
 *   ./scripts/run.py -c 1 -e /tests/tst-fork-file-mmap.so
 */

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cerrno>

int main()
{
    printf("=== tst-fork-file-mmap ===\n");
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);

    // A read-only ROFS file present on the test image (~47 KiB).
    const char *path = "/rofs/mmap-file-test1";
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        // fall back to the /tmp copy the image also ships
        fd = open("/tmp/mmap-file-test1", O_RDONLY);
    }
    if (fd < 0) { printf("FAIL: open %s (%s)\n", path, strerror(errno)); return 1; }

    struct stat st;
    if (fstat(fd, &st) != 0) { printf("FAIL: fstat\n"); return 1; }
    size_t size = (size_t)st.st_size;
    if (size < 6 * page) { printf("FAIL: file too small (%zu)\n", size); return 1; }

    // Target page the child will fault: a mid-file page (well past page 0) that
    // the parent never touches through the mapping.
    const size_t target_pg = 5;
    const off_t target_off = (off_t)(target_pg * page);

    // Read the EXPECTED bytes of that page via a SEPARATE fd + pread -- this
    // does not fault the mapping's PTE, so the child still demand-faults it.
    unsigned char *want = new unsigned char[page];
    {
        int fd2 = open(path, O_RDONLY);
        if (fd2 < 0) fd2 = open("/tmp/mmap-file-test1", O_RDONLY);
        if (fd2 < 0) { printf("FAIL: open2\n"); return 1; }
        ssize_t n = pread(fd2, want, page, target_off);
        close(fd2);
        if (n != (ssize_t)page) { printf("FAIL: pread expected page (%zd)\n", n); return 1; }
    }

    // mmap the file MAP_PRIVATE, read-only.  Do NOT touch the target page.
    unsigned char *p = (unsigned char *)mmap(nullptr, size, PROT_READ,
                                             MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) { printf("FAIL: mmap (%s)\n", strerror(errno)); return 1; }
    close(fd);  // mmap holds its own fileref

    pid_t pid = fork();
    if (pid == 0) {
        // CHILD: first access to the target page -> demand fault.  On buggy HEAD
        // this reads a zero-filled anon page; with the fix it reads file bytes.
        volatile unsigned char got0 = p[target_off];
        int bad = memcmp((const void *)(p + target_off), want, page);
        printf("child: page %zu byte0=0x%02x want=0x%02x %s\n",
               target_pg, (unsigned)got0, (unsigned)want[0],
               bad ? "MISMATCH" : "OK");
        _exit(bad ? 70 : 55);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    munmap(p, size);
    delete[] want;

    if (WIFEXITED(status) && WEXITSTATUS(status) == 55) {
        printf("PASS: fork child reads real file bytes from an untouched "
               "file-backed mmap page\n");
        return 0;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 70) {
        printf("FAIL/BUG [W-branch]: child saw ZEROS/wrong data -- file_vma "
               "cloned as anon_vma (status=0x%x)\n", status);
    } else {
        printf("FAIL: child aborted (status=0x%x)\n", status);
    }
    return 1;
}
