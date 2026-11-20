/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// A fork child's private memory must keep what it held at the fork, whatever
// the parent does with the same addresses later.  This test checks that for
// memory the parent later hands to a thread as its stack.
//
// The parent fills a private mapping and forks a first child, so the child
// maps the filled pages copy-on-write.  The parent then starts a thread on
// that mapping (pthread_attr_setstack), which puts it among the thread
// stacks that every later fork shares with the child instead of copying.  A
// second fork runs, the parent overwrites the filled pages, and the first
// child, which has been waiting, must still read the values from before its
// fork.  The second child is checked too: it shares the stack with the parent
// and reads what the parent held at the second fork.  The whole sequence then
// repeats, and free memory must come back once the children and the thread
// are gone: the page the parent copied away from must be freed.

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>

static const size_t PG = 4096;
static const size_t STACK = 1 << 20;
static const size_t STACK_TOP = 256 << 10;   // the thread's own frames
static const size_t FILLED = STACK - STACK_TOP;
static const int LEAK_ROUNDS = 100;
// A round fills FILLED bytes of stack; losing the old copy of each page loses
// most of them.  Allow half: the kernel's own per-thread and per-child memory
// moves free memory by up to about a quarter of that.
static const long LIMIT_BYTES_PER_ROUND = FILLED / 2;

static int failures;

static void expect(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

static volatile uint32_t *word(char *base, size_t off)
{
    return reinterpret_cast<volatile uint32_t *>(base + off);
}

static uint32_t before_fork(size_t off)
{
    return 0x51ac0000u + (uint32_t)(off / PG);
}

static uint32_t after_fork(size_t off)
{
    return 0xa47e0000u + (uint32_t)(off / PG);
}

// The number of filled pages that do not hold @want(off).
static int mismatches(char *st, uint32_t (*want)(size_t))
{
    int bad = 0;
    for (size_t off = 0; off < FILLED; off += PG) {
        if (*word(st, off) != want(off)) {
            bad++;
        }
    }
    return bad;
}

static void *park(void *arg)
{
    char c;
    if (read(*static_cast<int *>(arg), &c, 1) != 1) {
        return arg;
    }
    return nullptr;
}

static int reap(pid_t pid)
{
    int st = 0;
    if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st)) {
        return -1;
    }
    return WEXITSTATUS(st);
}

static void check(bool verbose, bool ok, const char *what)
{
    if (verbose || !ok) {
        expect(ok, what);
    }
}

static long free_bytes()
{
    struct sysinfo si;
    if (sysinfo(&si) != 0) {
        return -1;
    }
    return (long)si.freeram * si.mem_unit;
}

// One round: fork a child over the filled mapping, make the mapping a thread
// stack, fork again, overwrite it, and check both children.  Prints each
// check when @verbose, and only failures otherwise.
static void round_trip(bool verbose)
{
    // Fill the stack's lower part only.  The top, where the thread will run,
    // stays unpopulated at the first fork, so the thread never starts on a
    // copy-on-write page.
    char *st = static_cast<char *>(mmap(nullptr, STACK, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    int go[2], park_pipe[2];
    if (st == MAP_FAILED || pipe(go) || pipe(park_pipe)) {
        printf("FAIL: setup (errno %d)\n", errno);
        failures++;
        return;
    }
    for (size_t off = 0; off < FILLED; off += PG) {
        *word(st, off) = before_fork(off);
    }

    pid_t first = fork();
    if (first == 0) {
        char c;
        if (read(go[0], &c, 1) != 1) {
            _exit(100);
        }
        int bad = mismatches(st, before_fork);
        if (verbose) {
            printf("first child: %d of %zu pages changed since its fork\n",
                   bad, FILLED / PG);
        }
        _exit(bad ? 1 : 0);
    }

    pthread_attr_t a;
    pthread_t t;
    pthread_attr_init(&a);
    pthread_attr_setstack(&a, st, STACK);
    if (pthread_create(&t, &a, park, &park_pipe[0])) {
        printf("FAIL: pthread_create (errno %d)\n", errno);
        failures++;
        return;
    }
    sleep(1);

    pid_t second = fork();
    if (second == 0) {
        _exit(mismatches(st, before_fork) ? 1 : 0);
    }
    check(verbose, reap(second) == 0, "the second child reads the stack as "
          "the parent held it at the second fork");

    for (size_t off = 0; off < FILLED; off += PG) {
        *word(st, off) = after_fork(off);
    }
    if (write(go[1], "g", 1) != 1) {
        printf("FAIL: pipe write\n");
        failures++;
    }
    check(verbose, reap(first) == 0, "the first child does not see the "
          "parent's writes to memory that became a stack after its fork");
    check(verbose, mismatches(st, after_fork) == 0,
          "the parent reads its own writes");

    if (write(park_pipe[1], "x", 1) != 1) {
        failures++;
    }
    pthread_join(t, nullptr);
    munmap(st, STACK);
    close(go[0]);
    close(go[1]);
    close(park_pipe[0]);
    close(park_pipe[1]);
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        printf("SUMMARY: 0 failures\n");
        // OSv can hold this line back after setvbuf(); flush it.
        fflush(stdout);
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    reap(probe);

    round_trip(true);

    for (int r = 0; r < 5; r++) {
        round_trip(false);
    }
    sleep(1);
    long before = free_bytes();
    for (int r = 0; r < LEAK_ROUNDS; r++) {
        round_trip(false);
    }
    sleep(1);
    long after = free_bytes();
    long per_round = (before - after) / LEAK_ROUNDS;
    printf("free memory %ld -> %ld KiB over %d rounds: %ld bytes per round "
           "(limit %ld)\n", before / 1024, after / 1024, LEAK_ROUNDS, per_round,
           LIMIT_BYTES_PER_ROUND);
    expect(per_round <= LIMIT_BYTES_PER_ROUND,
           "memory comes back after each round");
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}
