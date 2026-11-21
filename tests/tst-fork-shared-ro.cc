/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Read-only MAP_SHARED memory must stay read-only across fork().  Two regions
// are checked: anonymous MAP_SHARED memory written and then mprotect()ed to
// PROT_READ (with one page left untouched until after the fork), and a POSIX
// shm object mapped PROT_READ from the start.  After the
// fork, a write to either region must raise SIGSEGV in the parent and in the
// child, and reads must see the data.  When the parent mprotect()s the
// anonymous region back to PROT_READ|PROT_WRITE, its writes work and the child
// sees them, while the child's own mapping stays read-only.  A second fork,
// with the region writable in the parent, checks that writes still flow both
// ways.  This is what Linux does.

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static const size_t PG = 4096;
static const size_t ANON = 3 * PG;   // page 2 is not touched before the fork
static const char *SHM_NAME = "/tst-fork-shared-ro";

static sigjmp_buf env;
static volatile int failures;

static void on_segv(int)
{
    siglongjmp(env, 1);
}

static void expect(bool ok, const char *who, const char *what)
{
    printf("%s: %s: %s\n", ok ? "PASS" : "FAIL", who, what);
    if (!ok) {
        failures++;
    }
}

// Store @v at @p; return true if the store raised SIGSEGV instead.
static bool write_faults(volatile uint32_t *p, uint32_t v)
{
    if (sigsetjmp(env, 1)) {
        return true;
    }
    *p = v;
    return false;
}

static volatile uint32_t *word(char *base, size_t off)
{
    return reinterpret_cast<volatile uint32_t *>(base + off);
}

// The checks both sides make while both regions are read-only to them.
static void check_read_only(const char *who, char *anon, char *shm)
{
    expect(*word(anon, 0) == 0xa0a0a0a0 && *word(anon, PG) == 0xa1a1a1a1,
           who, "reads the read-only anonymous shared region");
    expect(*word(shm, 0) == 0xb0b0b0b0, who, "reads the read-only shm region");
    expect(write_faults(word(anon, 0), 1), who,
           "a write to the read-only anonymous shared region faults");
    expect(write_faults(word(anon, PG), 2), who,
           "a write to its second page faults");
    expect(*word(anon, 2 * PG) == 0, who, "reads the untouched page as zero");
    expect(write_faults(word(anon, 2 * PG), 6), who,
           "a write to the page first touched after the fork faults");
    expect(write_faults(word(shm, 0), 3), who,
           "a write to the read-only shm region faults");
    expect(*word(anon, 0) == 0xa0a0a0a0 && *word(shm, 0) == 0xb0b0b0b0,
           who, "the faulting writes changed nothing");
}

static void child_first(char *anon, char *shm, int to_parent, int from_parent)
{
    char c = 'r';
    failures = 0;
    check_read_only("child", anon, shm);
    if (write(to_parent, &c, 1) != 1 || read(from_parent, &c, 1) != 1) {
        _exit(100);
    }
    // The parent has made its own mapping writable and written page 0.
    expect(*word(anon, 0) == 0xc0c0c0c0, "child",
           "sees the parent's write after the parent's mprotect(RW)");
    expect(write_faults(word(anon, PG), 4), "child",
           "its own mapping is still read-only");
    _exit(failures);
}

static void child_second(char *anon)
{
    failures = 0;
    expect(!write_faults(word(anon, 0), 0xe0e0e0e0), "child 2",
           "writes the region the parent forked writable");
    expect(*word(anon, PG) == 0xd1d1d1d1, "child 2", "sees the parent's write");
    _exit(failures);
}

static int reap(pid_t pid)
{
    int st = 0;
    if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st)) {
        return -1;
    }
    return WEXITSTATUS(st);
}

int main()
{
    // Unbuffered, so a child's lines are not lost when it calls _exit().
    setvbuf(stdout, nullptr, _IONBF, 0);
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        // OSv can hold this line back after setvbuf(); flush it.
        fflush(stdout);
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    reap(probe);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_segv;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);

    // The anonymous region: written, then made read-only.
    auto anon = static_cast<char *>(mmap(nullptr, ANON, PROT_READ | PROT_WRITE,
                                         MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    // The shm region: filled through a writable mapping that is then dropped,
    // and mapped read-only from the start.
    shm_unlink(SHM_NAME);
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (anon == MAP_FAILED || fd < 0 || ftruncate(fd, PG) != 0) {
        printf("FAIL: setup (errno %d)\n", errno);
        return 1;
    }
    auto fill = static_cast<char *>(mmap(nullptr, PG, PROT_READ | PROT_WRITE,
                                         MAP_SHARED, fd, 0));
    auto shm = static_cast<char *>(mmap(nullptr, PG, PROT_READ, MAP_SHARED, fd, 0));
    if (fill == MAP_FAILED || shm == MAP_FAILED) {
        printf("FAIL: shm mmap (errno %d)\n", errno);
        return 1;
    }
    *word(fill, 0) = 0xb0b0b0b0;
    munmap(fill, PG);
    *word(anon, 0) = 0xa0a0a0a0;
    *word(anon, PG) = 0xa1a1a1a1;
    expect(mprotect(anon, ANON, PROT_READ) == 0, "parent", "mprotect(PROT_READ)");
    // Touch both regions (but not page 2 of the anonymous one) so their pages
    // are mapped when the fork copies them.
    expect(*word(anon, 0) == 0xa0a0a0a0 && *word(shm, 0) == 0xb0b0b0b0, "parent",
           "reads both regions before the fork");

    int to_parent[2], from_parent[2];
    if (pipe(to_parent) != 0 || pipe(from_parent) != 0) {
        printf("FAIL: pipe\n");
        return 1;
    }
    pid_t pid = fork();
    if (pid == 0) {
        child_first(anon, shm, to_parent[1], from_parent[0]);
    }
    char c = 0;
    expect(pid > 0 && read(to_parent[0], &c, 1) == 1, "parent", "fork and child ready");
    check_read_only("parent", anon, shm);
    expect(mprotect(anon, ANON, PROT_READ | PROT_WRITE) == 0, "parent",
           "mprotect(RW) of its own mapping");
    expect(!write_faults(word(anon, 0), 0xc0c0c0c0), "parent",
           "a write after its own mprotect(RW) works");
    if (write(from_parent[1], &c, 1) != 1) {
        printf("FAIL: pipe write\n");
        return 1;
    }
    int code = reap(pid);
    expect(code == 0, "parent", "the first child's checks held");
    expect(*word(anon, 2 * PG) == 0, "parent",
           "the untouched page is still zero after the child exits");
    *word(anon, PG) = 0xd1d1d1d1;
    expect(write_faults(word(shm, 0), 5), "parent",
           "the shm region is still read-only after the child exits");

    pid = fork();
    if (pid == 0) {
        child_second(anon);
    }
    code = reap(pid);
    expect(code == 0, "parent", "the second child's checks held");
    expect(*word(anon, 0) == 0xe0e0e0e0, "parent", "sees the second child's write");

    munmap(shm, PG);
    munmap(anon, ANON);
    close(fd);
    shm_unlink(SHM_NAME);
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
