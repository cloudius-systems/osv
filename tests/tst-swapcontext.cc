/*
 * Copyright (C) 2026 Nadav Har'El
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Check that swapcontext() switches between two contexts back and forth, as
// coroutine and fiber libraries use it: main() and a coroutine take turns,
// each resuming the other with swapcontext(), and each must continue right
// after its own swapcontext() call. At the end the coroutine returns, which
// must switch to its uc_link, main().
//
// If resuming a context saved by swapcontext() switched away again instead of
// returning, the two contexts would bounce between each other forever without
// running any code, so a watchdog thread fails the test instead of letting it
// hang.

#include <ucontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <chrono>
#include <thread>

static ucontext_t main_ctx, co_ctx;
static char co_stack[65536];
static const int rounds = 5;
// The order in which the two contexts ran: main writes even values,
// the coroutine odd ones.
static int log_[2 * rounds + 1];
static int nlog;
static bool co_finished;

static void coroutine()
{
    for (int i = 0; i < rounds; i++) {
        log_[nlog++] = 2 * i + 1;
        swapcontext(&co_ctx, &main_ctx);
    }
    co_finished = true;
    // Returning switches to uc_link, main_ctx.
}

int main()
{
    std::atomic<bool> done{false};
    std::thread watchdog([&] {
        for (int i = 0; i < 100 && !done; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!done) {
            printf("FAIL: swapcontext() did not return to its caller "
                   "within 10 seconds\n");
            abort();
        }
    });

    getcontext(&co_ctx);
    co_ctx.uc_stack.ss_sp = co_stack;
    co_ctx.uc_stack.ss_size = sizeof(co_stack);
    co_ctx.uc_link = &main_ctx;
    makecontext(&co_ctx, coroutine, 0);

    for (int i = 0; i <= rounds; i++) {
        log_[nlog++] = 2 * i;
        swapcontext(&main_ctx, &co_ctx);
    }
    done = true;
    watchdog.join();

    bool ok = co_finished && nlog == 2 * rounds + 1;
    for (int i = 0; ok && i < nlog; i++) {
        ok = log_[i] == i;
    }
    if (!ok) {
        printf("FAIL: wrong order of execution:");
        for (int i = 0; i < nlog; i++) {
            printf(" %d", log_[i]);
        }
        printf(" (coroutine %s)\n", co_finished ? "finished" : "not finished");
        return 1;
    }
    printf("PASS: %d switches in each direction\n", rounds + 1);
    return 0;
}
