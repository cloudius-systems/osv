/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Test for kill() and alarm() approximations in libc/signal.cc

#include <sys/types.h>
#include <signal.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

int global = 0;

void handler1(int sig) {
    printf("handler1 called, sig=%d, global=%d\n", sig, global);
    global = 1;
}

// Test kill() on the current process, sending
// "pid" to kill should cause this process to be interrupted
// and signal handler for SIGUSR1 should already be installed
void test_signal_self(pid_t pid){
    int r;
    char output[64];

    global = 0;
    r = kill(pid, SIGUSR1);
    snprintf(output, 64, "kill SIGUSR1 and pid %d succeeds", pid);
    report(r == 0, output);

    for(int i = 0; i < 100; i++){
        if(global == 1) break;
        usleep(10000);
    }
    snprintf(output, 64, "global now 1, process correctly interrupted with pid %d", pid);
    report(global == 1, output);
}

// test kill() edges cases, pid should be a valid pid
void test_edge_cases(pid_t pid){
    int r;
    char output[64];

    // signal 0 always succeeds with valid pid
    r = kill(pid, 0);
    snprintf(output, 64, "kill succeeds with pid %d and signal 0", pid);
    report(r == 0, output);

    // kill with invalid signal number
    r = kill(pid, -2);
    snprintf(output, 64, "kill with pid %d and invalid signal number", pid);
    report(r == -1 && errno == EINVAL, output);

    // another invalid signal number
    r = kill(pid, 12345);
    snprintf(output, 64, "kill with pid %d and invalid signal number", pid);
    report(r == -1 && errno == EINVAL, output);
}

int main(int ac, char** av)
{
    // Test kill() of current process:
    report (global == 0, "'global' initially 0");
    auto sr = signal(SIGUSR1, handler1);
    report(sr != SIG_ERR, "set SIGUSR1 handler");

    // pid 0 = "all processes whose process group ID is
    //          equal to process group ID of sender"
    test_signal_self(0);

    // pid -1 = "all processes for which the calling process
    //           has permission to send signals"
    test_signal_self(-1);

    // our own pid
    test_signal_self(getpid());

    // Test various edge cases for kill() with various pids
    test_edge_cases(0);
    test_edge_cases(-1);
    test_edge_cases(getpid());

    int r;
    r = kill(17171717, 0);
    report(r == -1 && errno == ESRCH, "kill of non-existant process");

    // Test alarm();
    global = 0;
    sr = signal(SIGALRM, handler1);
    report(sr != SIG_ERR, "set SIGALRM handler");
    auto ar = alarm(1);
    report(ar == 0, "set alarm for 1 second - no previous alarm");
    usleep(500000);
    report(global == 0, "after 0.5 seconds - still global==0");
    sleep(1);
    report(global == 1, "after 1 more second - now global==1");

    // Test cancel of alarm();
    global = 0;
    ar = alarm(1);
    report(ar == 0, "set alarm for 1 second - no previous alarm");
    usleep(500000);
    report(global == 0, "after 0.5 seconds - still global==0");
    ar = alarm(0);
    sleep(1);
    report(global == 0, "1 more second after cancelling alarm - still global==0");

    //Test that SIG_ALRM interrupts system calls
    // TODO: a previous version of this test used signal(), but on Linux
    // signal() restarts system calls, so this test would fail, so we need
    // to use sigaction() here. We should add a test verifying that on OSv
    // signal() also doesn't interrupt networking system calls (however,
    // currently such a test would fail).
    //sr = signal(SIGALRM, handler1);
    struct sigaction a = {};
    a.sa_handler = handler1;
    r = sigaction(SIGALRM, &a, nullptr);
    report(r == 0, "set SIGALRM handler");

    auto s = socket(AF_INET,SOCK_DGRAM,0);
    char buf[10];
    struct sockaddr_storage ra;
    socklen_t ralen = sizeof(ra);

    alarm(1);

    auto recv_result = recvfrom(s, buf, sizeof(buf), 0,
                                (struct sockaddr*)&ra, &ralen);
    auto recv_errno = errno;

    report(recv_result == -1,   "syscall interrupted by SIG_ALRM returns -1");
    report(recv_errno == EINTR, "errno for syscall interrupted by SIG_ALRM is EINTR");

    //Test that SIG_ALRM doesn't interrupt system calls when
    //signal action is SIG_IGN
    sr = signal(SIGALRM, SIG_IGN);
    report(sr != SIG_ERR, "set SIGALRM handlerto SIG_IGN");

    struct timeval tv = {0};
    tv.tv_usec = 500000;
    auto res = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    report(res == 0, "set socket receive timeout 0.5 seconds");

    alarm(1);

    recv_result = recvfrom(s, buf, sizeof(buf), 0,
                           (struct sockaddr*)&ra, &ralen);
    recv_errno = errno;

    report(recv_result == -1,   "timed out syscall returns -1");
    report(recv_errno == EWOULDBLOCK, "timed out syscall doesn't return EINTR");

    // Test with and without SA_RESETHAND (for __sysv_signal support)
    struct sigaction act = {};
    act.sa_handler = handler1;
    global = 0;
    r = sigaction(SIGUSR1, &act, nullptr);
    report(r == 0, "set SIGUSR1 handler");

    // ensure signal handler is called when kill is
    // called with any of these pids
    test_signal_self(0);
    test_signal_self(-1);
    test_signal_self(getpid());

    struct sigaction oldact;
    r = sigaction(SIGUSR1, nullptr, &oldact);
    report(r == 0 && oldact.sa_handler == handler1, "without SA_RESETHAND, signal handler is not reset");
    act.sa_flags = SA_RESETHAND;
    global = 0;
    r = sigaction(SIGUSR1, &act, nullptr);
    report(r == 0, "set SIGUSR1 handler with SA_RESETHAND");

    // ensure signal handler is called when kill is
    // called with any of these pids
    test_signal_self(0);
    test_signal_self(-1);
    test_signal_self(getpid());

    r = sigaction(SIGUSR1, nullptr, &oldact);
    report(r == 0 && oldact.sa_handler == SIG_DFL, "with SA_RESETHAND, signal handler is reset");


    printf("SUMMARY: %d tests, %d failures\n", tests, fails);

    // At this point, handler1 might still be running, and if we return this
    // module, including handler1, might be unmapped. So sleep to make sure
    // that handler1 is done.
    sleep(1);

    return fails == 0 ? 0 : 1;
}


