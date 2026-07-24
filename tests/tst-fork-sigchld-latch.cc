/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * REGRESSION TEST for the PostgreSQL postmaster SIGCHLD + latch wakeup wall.
 *
 * This mirrors exactly what PostgreSQL's postmaster does to sequence the
 * auxiliary processes after crash recovery:
 *
 *   - The postmaster installs a real SIGCHLD handler (its reaper).  The handler
 *     sets a "latch" by writing one byte to a self-pipe (PG built on OSv with
 *     -DWAIT_USE_SELF_PIPE).
 *   - The postmaster's server loop parks in epoll_wait() on a set that includes
 *     the self-pipe READ end.
 *   - When a child (e.g. the startup process) exits, the kernel must deliver
 *     SIGCHLD to the postmaster, its handler must run and write the self-pipe,
 *     and that write must wake the postmaster's epoll_wait so the reaper can
 *     waitpid() the child and launch the next process.
 *
 * On OSv fork() is thread-backed: the child is a thread in its own COW address
 * space.  When the child exits, fork's child-exit path raises SIGCHLD to the
 * parent (kill(getpid(), SIGCHLD)).  OSv delivers a caught signal by spawning a
 * fresh "signal_handler" thread to run the handler.  That thread inherits the
 * CREATING thread's address space -- and the creating thread here is the
 * EXITING CHILD, so before the fix the SIGCHLD handler ran in the child's
 * private COW address space, not the parent's.  The handler's write to the
 * self-pipe, and its access to the latch flag, then happened against the wrong
 * (dying) address space, so the parent's epoll_wait never woke.  The postmaster
 * hangs waiting for a wakeup that never comes.
 *
 * This test reproduces that hang directly and asserts recovery:
 *   parent installs a SIGCHLD handler that write()s a self-pipe;
 *   parent forks a child that just _exit(0)s;
 *   parent epoll_wait()s on the self-pipe read end;
 *   the fork child's exit must -> SIGCHLD -> handler -> self-pipe write ->
 *   epoll_wait wakes; then waitpid() reaps the child.
 *
 * On HEAD (before the fix) the epoll_wait times out (no wakeup) -> FAIL/hang.
 * With the fix, epoll_wait wakes promptly -> PASS.
 *
 * To reproduce (in arena-dev, CONF_fork=y build):
 *   ./scripts/build conf_fork=1 fs=rofs image=tests, then:
 *   ./scripts/run.py -c 2 -e /tests/tst-fork-sigchld-latch.so
 */

#include <sys/epoll.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <cerrno>

// The self-pipe, exactly like PG's latch: SIGCHLD handler writes one byte to
// the write end; the server loop epoll_waits on the read end.
static int g_selfpipe[2] = { -1, -1 };
// Set by the handler so we can also confirm the handler actually ran.
static volatile sig_atomic_t g_handler_ran = 0;

// PG's reaper does almost nothing in signal context: it just pokes the latch
// (writes the self-pipe) so the main loop wakes and does the real reaping.
static void sigchld_handler(int)
{
    g_handler_ran = 1;
    char c = 'x';
    // async-signal-safe write, exactly like PG's latch WakeupMyLatch.
    (void)!write(g_selfpipe[1], &c, 1);
}

int main()
{
    printf("=== tst-fork-sigchld-latch ===\n");

    if (pipe(g_selfpipe) < 0) {
        printf("FAIL: pipe() errno=%d\n", errno);
        return 1;
    }
    // Non-blocking, like PG's self-pipe.
    fcntl(g_selfpipe[0], F_SETFL, O_NONBLOCK);
    fcntl(g_selfpipe[1], F_SETFL, O_NONBLOCK);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, nullptr) < 0) {
        printf("FAIL: sigaction(SIGCHLD) errno=%d\n", errno);
        return 1;
    }

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        printf("FAIL: epoll_create1() errno=%d\n", errno);
        return 1;
    }
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = g_selfpipe[0];
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, g_selfpipe[0], &ev) < 0) {
        printf("FAIL: epoll_ctl(ADD) errno=%d\n", errno);
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // CHILD: like a PostgreSQL startup process, RESET the inherited SIGCHLD
        // disposition to the default before exiting.  With a single global
        // signal table this clobbers the parent's reaper handler, so the
        // parent is never notified when this child exits -- the postmaster
        // hang.  With per-process signal dispositions the parent's handler is
        // untouched.
        signal(SIGCHLD, SIG_DFL);
        _exit(0);
    }
    if (pid < 0) {
        printf("FAIL: fork() errno=%d\n", errno);
        return 1;
    }
    printf("forked child pid=%d; parent now waiting on the self-pipe via "
           "epoll (mirrors PG postmaster ServerLoop)\n", pid);

    // Park in epoll_wait exactly like the postmaster ServerLoop.  The child's
    // exit must wake us within the timeout via SIGCHLD -> handler -> self-pipe.
    struct epoll_event out[4];
    int nfds = epoll_wait(epfd, out, 4, 5000 /* ms */);
    if (nfds <= 0) {
        printf("FAIL: epoll_wait returned %d (errno=%d) -- SIGCHLD did not wake "
               "the parent's epoll (handler_ran=%d).  This is the postmaster "
               "hang.\n", nfds, errno, (int)g_handler_ran);
        return 1;
    }

    // Drain the self-pipe byte, like PG does.
    char buf[16];
    (void)!read(g_selfpipe[0], buf, sizeof(buf));

    printf("epoll woke (nfds=%d, handler_ran=%d); now reaping child\n",
           nfds, (int)g_handler_ran);

    // The reaper (main loop) waitpid()s the child, exactly like PG.
    int st = 0;
    pid_t r = waitpid(pid, &st, 0);
    if (r != pid) {
        printf("FAIL: waitpid returned %d (want %d) errno=%d -- reaper could "
               "not reap the child\n", r, pid, errno);
        return 1;
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("FAIL: child status 0x%x (want normal exit 0)\n", st);
        return 1;
    }

    if (!g_handler_ran) {
        printf("FAIL: epoll woke but the SIGCHLD handler never ran\n");
        return 1;
    }

    printf("PASS: child exit -> SIGCHLD -> handler wrote self-pipe -> epoll "
           "woke -> waitpid reaped child (pid=%d, status=0x%x)\n", pid, st);
    return 0;
}
