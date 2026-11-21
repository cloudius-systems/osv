/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A read-only private page that a fork shares with the child must stay the
// child's until the child drops it, whatever the parent does with its own
// mapping afterwards.  The parent maps anonymous memory, fills it, makes it
// read-only with mprotect(), and forks.  Then, while the child still maps it:
//   unmap   - the parent munmap()s it and reuses memory: the frames must not
//             be freed under the child;
//   protect - the parent mprotect()s it writable again and writes it: the
//             write must not reach the child.
//   segv    - the parent writes it without mprotect(): the write must raise
//             SIGSEGV (the handler then makes the page writable and the write
//             is retried, and it must not reach the child either).
// Each case runs on 4 KiB pages (a 256 KiB mapping) and on 2 MiB pages (an
// 8 MiB mapping, which gets 2 MiB pages in its aligned middle).  The child
// checks every page and exits with the number it found changed.

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static const size_t KiB = 1024, MiB = 1024 * KiB;
static const char PATTERN = 'R';

static int bad_pages(const char *p, size_t n)
{
    int bad = 0;
    for (size_t off = 0; off < n; off += 4096) {
        if (p[off] != PATTERN || p[off + 64] != PATTERN ||
                p[off + 2048] != PATTERN || p[off + 4095] != PATTERN) {
            bad++;
        }
    }
    return bad;
}

// Free and reuse memory on this CPU: freed pages pass through a per-CPU
// quarantine before they can be handed out again.
static void churn()
{
    for (int i = 0; i < 16; i++) {
        char *q = static_cast<char *>(mmap(nullptr, MiB, PROT_READ | PROT_WRITE,
                                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (q != MAP_FAILED) {
            memset(q, 0xaa, MiB);
            munmap(q, MiB);
        }
    }
    for (int i = 0; i < 64; i++) {
        char *q = static_cast<char *>(mmap(nullptr, MiB, PROT_READ | PROT_WRITE,
                                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (q != MAP_FAILED) {
            memset(q, 0xaa, MiB);   // kept until the guest exits
        }
    }
    char *h = static_cast<char *>(mmap(nullptr, 32 * MiB, PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (h != MAP_FAILED) {
        memset(h, 0xaa, 32 * MiB);
    }
}

static volatile int segv_count;

static void segv_handler(int, siginfo_t *si, void *)
{
    segv_count++;
    uintptr_t page = reinterpret_cast<uintptr_t>(si->si_addr) & ~uintptr_t(4095);
    mprotect(reinterpret_cast<void *>(page), 4096, PROT_READ | PROT_WRITE);
}

enum class action { protect, unmap, segv };

static bool run_case(const char *name, size_t size, action act)
{
    bool unmap = act == action::unmap;
    char *p = static_cast<char *>(mmap(nullptr, size, PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (p == MAP_FAILED) {
        printf("FAIL: %s: mmap errno=%d\n", name, errno);
        return false;
    }
    memset(p, PATTERN, size);
    if (mprotect(p, size, PROT_READ)) {
        printf("FAIL: %s: mprotect errno=%d\n", name, errno);
        return false;
    }
    int go[2], ready[2];
    if (pipe(go) || pipe(ready)) {
        printf("FAIL: %s: pipe errno=%d\n", name, errno);
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        printf("FAIL: %s: fork errno=%d\n", name, errno);
        return false;
    }
    if (pid == 0) {
        close(go[1]);
        close(ready[0]);
        int before = bad_pages(p, size);
        char c = 'r';
        if (write(ready[1], &c, 1) != 1 || read(go[0], &c, 1) != 1) {
            _exit(255);
        }
        int after = bad_pages(p, size);
        _exit(before ? 254 : (after > 250 ? 250 : after));
    }
    close(go[0]);
    close(ready[1]);
    char c;
    if (read(ready[0], &c, 1) != 1) {
        printf("FAIL: %s: child did not start\n", name);
        return false;
    }
    close(ready[0]);
    if (act == action::unmap) {
        munmap(p, size);
        churn();
    } else if (act == action::segv) {
        struct sigaction sa = {}, old;
        sa.sa_sigaction = segv_handler;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, &old);
        segv_count = 0;
        for (size_t off = 0; off < size; off += 4096) {
            *(volatile char *)(p + off) = 0x55;
        }
        sigaction(SIGSEGV, &old, nullptr);
        if (segv_count != int(size / 4096)) {
            printf("FAIL: %s: %d of %zu writes to read-only pages raised SIGSEGV\n",
                   name, segv_count, size / 4096);
            write(go[1], "g", 1);
            waitpid(pid, nullptr, 0);
            return false;
        }
    } else {
        mprotect(p, size, PROT_READ | PROT_WRITE);
        memset(p, 0x55, size);
    }
    if (write(go[1], "g", 1) != 1) {
        printf("FAIL: %s: pipe write errno=%d\n", name, errno);
        return false;
    }
    close(go[1]);
    int st = 0;
    if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st)) {
        printf("FAIL: %s: child did not exit (status 0x%x)\n", name, st);
        return false;
    }
    int bad = WEXITSTATUS(st);
    if (bad == 254) {
        printf("FAIL: %s: the child's copy was wrong before the parent changed its own\n", name);
        return false;
    }
    if (bad) {
        printf("FAIL: %s: the parent's %s changed %d of the child's %zu read-only pages\n",
               name, unmap ? "munmap" : "write", bad, size / 4096);
        return false;
    }
    printf("%s: the child's %zu read-only pages are intact\n", name, size / 4096);
    if (!unmap) {
        munmap(p, size);
    }
    if (act == action::segv) {
        printf("%s: every write to the %zu read-only pages raised SIGSEGV\n", name, size / 4096);
    }
    return true;
}

int main()
{
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        printf("SUMMARY: 0 failures\n");
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    waitpid(probe, nullptr, 0);
    setvbuf(stdout, nullptr, _IONBF, 0);

    // Keep the frees and the reuse on one CPU (see churn()).
    cpu_set_t cs;
    CPU_ZERO(&cs);
    CPU_SET(0, &cs);
    pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);

    int failures = 0;
    failures += !run_case("segv-4k", 256 * KiB, action::segv);
    failures += !run_case("segv-2m", 8 * MiB, action::segv);
    failures += !run_case("protect-4k", 256 * KiB, action::protect);
    failures += !run_case("protect-2m", 8 * MiB, action::protect);
    failures += !run_case("unmap-4k", 256 * KiB, action::unmap);
    failures += !run_case("unmap-2m", 8 * MiB, action::unmap);
    if (!failures) {
        printf("PASS: read-only pages shared at fork keep their contents for the child\n");
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
