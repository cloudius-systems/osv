/*
 * Copyright (C) 2021 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
// To compile on Linux, use: g++ -std=c++11 tests/tst-sigaction.cc
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <cassert>

static int global = 0;
void test_handler(int sig, siginfo_t *info, void *ucontext)
{
    printf("test_handler called, sig=%d, global=%d\n", sig, global);
    global = 1;
}

void test_sigaction_with_handler(void (*handler)(int, siginfo_t *, void *))
{
    struct sigaction act = {};
    act.sa_flags = SA_SIGINFO;
    sigemptyset(&act.sa_mask);
    act.sa_sigaction = handler;
    assert(0 == sigaction(SIGTERM, &act, nullptr));
    assert(0 == kill(getpid(), SIGTERM));
}

int main(int ac, char** av)
{
    test_sigaction_with_handler(test_handler);
    for(int i = 0; i < 100; i++){
        if(global == 1) break;
        usleep(10000);
    }
    assert(global == 1);
    printf("global now 1, test_handler called\n");

    test_sigaction_with_handler(nullptr);
    sleep(1);
    assert(0); //We should not get here as the app and OSv should have already terminated by this time
}
