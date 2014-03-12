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

#include <osv/debug.hh>

int tests = 0, fails = 0;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    debug("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

int global = 0;

void handler1(int sig) {
    debug("handler1 called, sig=%d\n", sig);
    global = 1;
}

int main(int ac, char** av)
{
    // Test kill() of current process:
    report (global == 0, "'global' initially 0");
    auto sr = signal(SIGUSR1, handler1);
    report(sr != SIG_ERR, "set SIGUSR1 handler");
    int r;
    r = kill(0, SIGUSR1);
    report(r == 0, "kill SIGUSR1 succeeds");
    for (int i=0; i<100; i++) {
        if (global == 1) break;
        usleep(10000);
    }
    report(global == 1, "'global' is now 1");
    // Test various edge cases for kill():
    r = kill(0, 0);
    report(r == 0, "kill with signal 0 succeeds (and does nothing)");
    r = kill(-1, 0);
    report(r == 0, "kill of pid -1 is also fine");
    r = kill(17, 0);
    report(r == -1 && errno == ESRCH, "kill of non-existant process");
    r = kill(0, -2);
    report(r == -1 && errno == EINVAL, "kill with invalid signal number");
    r = kill(0, 12345);
    report(r == -1 && errno == EINVAL, "kill with invalid signal number");

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
    sr = signal(SIGALRM, handler1);
    report(sr != SIG_ERR, "set SIGALRM handler");

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
    r = kill(0, SIGUSR1);
    report(r == 0, "kill SIGUSR1 succeeds");
    for (int i=0; i<100; i++) {
        if (global == 1) break;
        usleep(10000);
    }
    report(global == 1, "'global' is now 1");
    struct sigaction oldact;
    r = sigaction(SIGUSR1, nullptr, &oldact);
    report(r == 0 && oldact.sa_handler == handler1, "without SA_RESETHAND, signal handler is not reset");
    act.sa_flags = SA_RESETHAND;
    r = sigaction(SIGUSR1, &act, nullptr);
    report(r == 0, "set SIGUSR1 handler with SA_RESETHAND");
    r = kill(0, SIGUSR1);
    report(r == 0, "kill SIGUSR1 succeeds");
    for (int i=0; i<100; i++) {
        if (global == 1) break;
        usleep(10000);
    }
    report(global == 1, "'global' is now 1");
    r = sigaction(SIGUSR1, nullptr, &oldact);
    report(r == 0 && oldact.sa_handler == SIG_DFL, "with SA_RESETHAND, signal handler is reset");


    debug("SUMMARY: %d tests, %d failures\n", tests, fails);
}


