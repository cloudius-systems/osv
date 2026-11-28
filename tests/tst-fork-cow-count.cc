/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// fork counts the address spaces that map a copy-on-write frame, and frees
// the frame when the last one drops it.  This test checks two properties of
// that count.
//
// First, the count saturates instead of wrapping.  Some counts are never
// dropped (a child's shared mapping of a user-supplied thread stack), so a
// long-lived forking process can push one frame's count past 2^32, and a
// wrapped count would let a later drop free a frame that is still mapped.
// The add and drop that the kernel uses are checked directly at the
// boundary, since reaching it through fork would take billions of forks.
//
// Second, fork does not count frames it never needs to count.  A thread
// stack that OSv allocated (an mmap_stack vma) stays shared across every fork
// for as long as it exists, and its munmap frees it, so fork leaves its count
// at zero however often it forks.  A stack the application supplied with
// pthread_attr_setstack is still counted once per fork, because a later fork
// may find it outside every shared range once its thread exits.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-cow-count requires CONF_fork\n");
    return 0;
}

#else

#include <osv/cow_count.hh>
#include <osv/mmu.hh>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static const int FORKS = 1000;
static const size_t PG = 4096;
static const size_t STACK = 1 << 20;

static int failures;

static void expect(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

// The add and drop the kernel uses, at the top of the counter's range and at
// zero.
static void check_saturation()
{
    std::atomic<uint32_t> c(mmu::cow_count_max - 2);
    mmu::cow_count_add(c);
    expect(c.load() == mmu::cow_count_max - 1, "add below the maximum adds one");
    mmu::cow_count_add(c);
    expect(c.load() == mmu::cow_count_max, "add reaches the maximum");
    mmu::cow_count_add(c);
    expect(c.load() == mmu::cow_count_max, "add at the maximum does not wrap");
    bool other = mmu::cow_count_drop(c);
    expect(other && c.load() == mmu::cow_count_max,
           "drop at the maximum keeps the count and reports another mapper");

    c.store(1);
    other = mmu::cow_count_drop(c);
    expect(other && c.load() == 0, "drop from one reports another mapper");
    other = mmu::cow_count_drop(c);
    expect(!other && c.load() == 0,
           "drop at zero leaves zero and hands the frame to the caller");
}

struct parked {
    int pipe_fd;
    volatile char *probe;   // a byte on the thread's own stack
    volatile bool ready;
};

static void *park(void *arg)
{
    auto p = static_cast<parked *>(arg);
    volatile char here = 1;
    p->probe = &here;
    p->ready = true;
    char c;
    if (read(p->pipe_fd, &c, 1) != 1) {
        return arg;
    }
    return nullptr;
}

static bool start(pthread_t *t, parked *p, int fds[2], pthread_attr_t *a)
{
    if (pipe(fds)) {
        return false;
    }
    p->pipe_fd = fds[0];
    p->probe = nullptr;
    p->ready = false;
    if (pthread_create(t, a, park, p)) {
        return false;
    }
    while (!p->ready) {
        usleep(1000);
    }
    return true;
}

static void stop(pthread_t t, int fds[2])
{
    if (write(fds[1], "x", 1) != 1) {
        failures++;
    }
    pthread_join(t, nullptr);
    close(fds[0]);
    close(fds[1]);
}

static int reap(pid_t pid)
{
    int st = 0;
    if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st)) {
        return -1;
    }
    return WEXITSTATUS(st);
}

// Fork FORKS children while two threads are parked, one on a stack OSv
// allocated and one on a stack the test supplied, and compare the counts of
// one stack page of each.  The last child stays alive while they are read,
// so a child still maps both pages.
static void check_stack_counts()
{
    char *user = static_cast<char *>(mmap(nullptr, STACK, PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (user == MAP_FAILED) {
        printf("FAIL: mmap (errno %d)\n", errno);
        failures++;
        return;
    }
    memset(user, 0, STACK);

    pthread_attr_t own, supplied;
    pthread_attr_init(&own);
    pthread_attr_init(&supplied);
    pthread_attr_setstack(&supplied, user, STACK);
    pthread_t t_own, t_supplied;
    parked p_own, p_supplied;
    int f_own[2], f_supplied[2];
    if (!start(&t_own, &p_own, f_own, &own) ||
        !start(&t_supplied, &p_supplied, f_supplied, &supplied)) {
        printf("FAIL: thread setup (errno %d)\n", errno);
        failures++;
        return;
    }

    long own_before = mmu::fork_cow_mappers(const_cast<char *>(p_own.probe));
    long sup_before = mmu::fork_cow_mappers(const_cast<char *>(p_supplied.probe));
    int bad = 0;
    for (int i = 0; i < FORKS - 1; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            _exit(0);
        }
        if (pid < 0 || reap(pid) != 0) {
            bad++;
        }
    }
    int go[2];
    if (pipe(go)) {
        failures++;
        return;
    }
    pid_t last = fork();
    if (last == 0) {
        char c;
        _exit(read(go[0], &c, 1) == 1 ? 0 : 1);
    }
    long own_after = mmu::fork_cow_mappers(const_cast<char *>(p_own.probe));
    long sup_after = mmu::fork_cow_mappers(const_cast<char *>(p_supplied.probe));
    if (write(go[1], "g", 1) != 1 || last < 0 || reap(last) != 0) {
        bad++;
    }
    close(go[0]);
    close(go[1]);
    expect(bad == 0, "every child forks and exits cleanly");

    printf("OSv-allocated stack page: count %ld before, %ld after %d forks\n",
           own_before, own_after, FORKS);
    printf("supplied stack page: count %ld before, %ld after %d forks\n",
           sup_before, sup_after, FORKS);
    expect(own_before == 0 && own_after == 0,
           "fork does not count an OSv-allocated thread stack");
    expect(sup_before >= 0 && sup_after - sup_before == FORKS,
           "fork counts each child's mapping of a supplied thread stack");

    stop(t_own, f_own);
    stop(t_supplied, f_supplied);
    munmap(user, STACK);
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    check_saturation();

    // The count table is set up by the first fork.
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        printf("SUMMARY: %d failures\n", failures);
        return failures ? 1 : 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    reap(probe);

    check_stack_counts();
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}

#endif
