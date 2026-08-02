/*
 * Copyright (C) 2026 Greg Burd
 *
 * BSD license (see LICENSE).
 *
 * Fork visibility / symbol-lookup probe -- PLT variant.  This binary is LINKED
 * against libforksym.so, so calls to forksym_fn_* go through the executable's
 * PLT and are resolved LAZILY by elf::object::resolve_pltgot ->
 * object::symbol() -> program::lookup(), which is the exact path that aborts
 * with "failed looking up symbol X" when the child cannot find a symbol that
 * exists.  We force a FORKED CHILD (and a pthread that loaded the lib) to be
 * the first to resolve, exercising object::visible()'s thread-keyed gate.
 */
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
// A subset of the 64 exported functions in libforksym.so.  Declared so the
// call site emits a PLT reference; the parent never calls them before fork.
long forksym_fn_0(long); long forksym_fn_1(long); long forksym_fn_2(long);
long forksym_fn_3(long); long forksym_fn_4(long); long forksym_fn_5(long);
long forksym_fn_6(long); long forksym_fn_7(long); long forksym_fn_8(long);
long forksym_fn_9(long); long forksym_fn_10(long); long forksym_fn_11(long);
long forksym_fn_12(long); long forksym_fn_13(long); long forksym_fn_14(long);
long forksym_fn_15(long);
}

static volatile int g_zero = 0;

// First-resolve a batch of PLT symbols from libforksym.so (a dynamic dep).
static long child_resolve_lib(int salt)
{
    long a = salt + g_zero;
    a += forksym_fn_0(a);  a += forksym_fn_1(a);  a += forksym_fn_2(a);
    a += forksym_fn_3(a);  a += forksym_fn_4(a);  a += forksym_fn_5(a);
    a += forksym_fn_6(a);  a += forksym_fn_7(a);  a += forksym_fn_8(a);
    a += forksym_fn_9(a);  a += forksym_fn_10(a); a += forksym_fn_11(a);
    a += forksym_fn_12(a); a += forksym_fn_13(a); a += forksym_fn_14(a);
    a += forksym_fn_15(a);
    return a ? a : 1;
}

// pthread that first-resolves from a non-main thread, then forks children that
// first-resolve, while the pthread is still alive (loader-thread present) and
// the main thread is elsewhere.
static void *worker(void *arg)
{
    int *fail = (int*)arg;
    for (int i = 0; i < 24; i++) {
        pid_t p = fork();
        if (p == 0) { long r = child_resolve_lib(i); _exit(r ? 42 : 7); }
        int st = 0; waitpid(p, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 42) {
            printf("FAIL: pthread-forked child %d PLT lookup failed (st=0x%x)\n", i, st);
            __sync_fetch_and_add(fail, 1);
        }
    }
    return nullptr;
}

int main(int, char**)
{
    fflush(stdout);
    int failures = 0;

    // (1) fork children that are FIRST to resolve libforksym PLT symbols.
    for (int i = 0; i < 16; i++) {
        pid_t p = fork();
        if (p == 0) { long r = child_resolve_lib(i); _exit(r ? 42 : 7); }
        int st = 0; waitpid(p, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 42) {
            printf("FAIL: child %d PLT lookup failed (st=0x%x) -- symbol-lookup fault\n", i, st);
            failures++;
        }
    }

    // (2) resolve from a pthread whose tid differs from main's; the pthread
    // forks children that first-resolve.  object::visible() keys on the current
    // thread; a mismatch here would make the lib invisible to the child.
    pthread_t th;
    pthread_create(&th, nullptr, worker, &failures);
    pthread_join(th, nullptr);

    if (failures == 0) {
        printf("PASS: forked children (main + pthread) resolved libforksym PLT symbols\n");
        printf("SUMMARY: 0 failures\n");
        return 0;
    }
    printf("SUMMARY: %d failures\n", failures);
    return 1;
}
