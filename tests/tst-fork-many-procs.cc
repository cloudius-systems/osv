/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// More live fork children than any fixed per-process table would hold.
// Each of 600 children makes 1000 small mallocs it never frees, checks that
// every one is fork-arena memory (COW-isolated, freed with the process), and
// blocks on a pipe until all 600 are alive at once.  Then all are released and
// reaped, and free memory and the arena's own counters must return to where
// they were.  A child whose heap went to the shared kernel heap instead would
// fail the contains() check and leave its heap behind after it exits.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-many-procs requires CONF_fork\n");
    return 0;
}

#else

#include <osv/fork_arena.hh>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static const int nchildren = 600;
static const int nallocs = 1000;
static const long tolerance = 16L << 20;

static long free_bytes()
{
    struct sysinfo si;
    sysinfo(&si);
    return (long)si.freeram * si.mem_unit;
}

static bool read_all(int fd, char *buf, int n)
{
    while (n > 0) {
        ssize_t r = read(fd, buf, n);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        buf += r;
        n -= r;
    }
    return true;
}

static void child(int ready_w, int release_r)
{
    char ok = 'Y';
    for (int i = 0; i < nallocs; i++) {
        size_t sz = 16 + (i * 37) % 497;
        char *p = static_cast<char*>(malloc(sz));
        if (!p || !fork_arena::contains(p)) {
            ok = 'N';
            break;
        }
        memset(p, i, sz);
    }
    write(ready_w, &ok, 1);
    char c;
    read(release_r, &c, 1);
    _exit(0);
}

// Fork every child, wait until all are alive, release and reap them.
// Returns the number of children whose heap was not all in the arena.
static int one_round(int *spawned)
{
    int ready[2], release[2];
    if (pipe(ready) || pipe(release)) {
        printf("FAIL: pipe errno=%d\n", errno);
        return -1;
    }
    std::vector<pid_t> kids;
    for (int i = 0; i < nchildren; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            printf("FAIL: fork %d errno=%d\n", i, errno);
            break;
        }
        if (pid == 0) {
            child(ready[1], release[0]);
        }
        kids.push_back(pid);
    }
    *spawned = kids.size();
    std::vector<char> res(kids.size());
    int bad = 0;
    if (!read_all(ready[0], res.data(), res.size())) {
        printf("FAIL: lost a child's report\n");
        bad++;
    }
    for (char c : res) {
        bad += c != 'Y';
    }
    std::vector<char> go(kids.size(), 'G');
    write(release[1], go.data(), go.size());
    for (auto pid : kids) {
        int st;
        waitpid(pid, &st, 0);
    }
    close(ready[0]); close(ready[1]); close(release[0]); close(release[1]);
    return bad;
}

#ifdef FORK_ARENA_HAS_STATS
static bool stats_back(const fork_arena::arena_stats &base)
{
    auto s = fork_arena::stats();
    return s.live_states <= base.live_states && s.extents_in_use <= base.extents_in_use;
}
#endif

int main()
{
    errno = 0;
    pid_t probe = fork();
    if (probe == -1 && errno == ENOSYS) {
        printf("SKIP: fork() not enabled\n");
        return 0;
    }
    if (probe == 0) {
        _exit(0);
    }
    waitpid(probe, nullptr, 0);

    int failures = 0, n;
    // A first round, so kernel state that grows once is not counted.
    one_round(&n);
    sleep(2);
    long before = free_bytes();
#ifdef FORK_ARENA_HAS_STATS
    auto base = fork_arena::stats();
#endif
    int bad = one_round(&n);
    printf("%d children alive at once, %d with heap outside the fork arena\n", n, bad);
    if (n != nchildren || bad) {
        printf("FAIL: every child's heap must be fork-arena memory\n");
        failures++;
    }
    // The reaper frees each child after waitpid() returns.
    long lost = 0;
    for (int i = 0; i < 200; i++) {
        lost = before - free_bytes();
        bool done = lost <= tolerance;
#ifdef FORK_ARENA_HAS_STATS
        done = done && stats_back(base);
#endif
        if (done) {
            break;
        }
        usleep(100000);
    }
    printf("free memory: %ld KiB not returned after the children exited\n", lost >> 10);
    if (lost > tolerance) {
        printf("FAIL: the children's memory was not returned\n");
        failures++;
    }
#ifdef FORK_ARENA_HAS_STATS
    auto s = fork_arena::stats();
    printf("arena: %lu live states (%lu before), %lu extents (%lu before)\n",
           s.live_states, base.live_states, s.extents_in_use, base.extents_in_use);
    if (!stats_back(base)) {
        printf("FAIL: arena state of exited children not released\n");
        failures++;
    }
#endif
    if (!failures) {
        printf("PASS: tst-fork-many-procs\n");
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}

#endif
