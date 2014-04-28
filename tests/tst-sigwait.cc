/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Test for sigwait

#include <sys/types.h>
#include <signal.h>
#include <sys/socket.h>

#include <thread>
#include <assert.h>
#include <iostream>

void empty_handler(int sig) { abort(); }

int siguser2_received = 0;
void handler2(int sig) { siguser2_received = 1;}

int main(int ac, char** av)
{
    int sig;
    auto sr = signal(SIGUSR1, empty_handler);
    assert(sr != SIG_ERR);

    sr = signal(SIGUSR2, handler2);
    assert(sr != SIG_ERR);

    sigset_t set, old_set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &set, &old_set);

    // Call sigprocmask inside the thread as well so we test state cleanup
    std::thread thread1([&] { kill(0, SIGUSR1); sigprocmask(SIG_BLOCK, &set, nullptr); });
    sigwait(&set, &sig);
    assert(sig == SIGUSR1);
    thread1.join();

    // Do it again to guarantee that we haven't left any state back from the
    // previous thread.
    std::thread thread1a([&] { kill(0, SIGUSR1); });
    sigwait(&set, &sig);
    assert(sig == SIGUSR1);
    thread1a.join();

    // Not in set, should receive this one
    std::thread thread2([] { kill(0, SIGUSR2); });
    while (siguser2_received == 0);
    thread2.join();

    std::cout << "PASSED\n";
    return 0;
}
