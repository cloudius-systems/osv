/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * Lazy-PLT-resolution-across-fork test.
 *
 * OSv resolves a shared object's PLT/GOT entries LAZILY: the first call to an
 * imported function traps into elf::object::resolve_pltgot(), which looks the
 * symbol up and writes the resolved address into the .got.plt slot.  With
 * CONF_fork, a forked child runs in a COW-cloned application address space.  If
 * a PLT slot was NOT resolved in the parent before fork, the CHILD is the first
 * to hit it, and the resolver runs in the child's COW address space.
 *
 * The bug this catches: the resolver dereferences per-object state and writes
 * the GOT slot; if any of that (the elf::object* stashed in .got.plt[1], the
 * resolver's mutable bookkeeping, or the GOT page itself) is stale/NULL in the
 * child's COW copy, resolve_pltgot faults on a NULL.  This is the same
 * fork-coherence class as the other fork walls, in the dynamic-linker path.
 *
 * To force the child to be first: fork() at the very top, then in the child
 * call a batch of libc functions that the parent NEVER calls, so their PLT
 * slots are guaranteed unresolved at fork time.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cmath>
#include <ctime>
#include <clocale>
#include <cwchar>
#include <cstdint>

// A pile of libc functions the parent does NOT call before fork.  Each is
// reached through the executable's PLT, so the child's first call to it forces
// lazy PLT resolution in the child's COW address space.  Kept behind a runtime
// flag so the compiler cannot fold them away, and referenced ONLY from the
// child path.
static volatile int g_zero = 0;

static long child_exercise_plt(void)
{
    long acc = 0;
    // math (libc / libm via PLT)
    acc += (long)llround(sqrt((double)(g_zero + 2.0)));
    acc += (long)lround(cbrt((double)(g_zero + 27.0)));
    acc += (long)ceil(log2((double)(g_zero + 8.0)));
    acc += (long)floor(exp2((double)(g_zero + 3.0)));
    // string / mem the parent never used
    char buf[64];
    strncpy(buf, "pltgot", sizeof(buf));
    acc += (long)strnlen(buf, sizeof(buf));
    acc += (long)strcspn(buf, "g");
    acc += (long)strspn("aaab", "a");
    // conversion / locale / wide
    acc += (long)strtol("123", NULL, 10);
    acc += (long)labs(-7);
    acc += (long)llabs(-11);
    acc += (long)wcslen(L"wide");
    // time
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    acc += (ts.tv_sec != 0) ? 1 : 0;
    // stdio the parent did not touch
    char *p = NULL; size_t n = 0;
    (void)p; (void)n;
    acc += (long)snprintf(buf, sizeof(buf), "%d-%ld", (int)g_zero, acc);
    return acc;
}

// A DIFFERENT pile the child never calls, so the parent (AS0) is first to
// resolve them -- inserting fresh nodes into the SAME object's symbol set after
// children have already inserted (and been reaped).  If a reaped child's node
// lived in its COW arena, this parent-side rehash/traversal dereferences it.
static long parent_exercise_plt(void)
{
    long acc = 0;
    acc += (long)llround(sin((double)(g_zero + 1.0)));
    acc += (long)lround(cos((double)(g_zero + 1.0)));
    acc += (long)ceil(tan((double)(g_zero + 0.5)));
    acc += (long)floor(atan2((double)(g_zero + 1.0), 2.0));
    acc += (long)llround(hypot(3.0, 4.0));
    char buf[64];
    acc += (long)strcasecmp("AbC", "abc") + 1;
    acc += (long)strncasecmp("AbCd", "abce", 3) + 1;
    acc += (long)strtoul("456", NULL, 10);
    acc += (long)strtoll("789", NULL, 10);
    acc += (long)memcmp("aa", "ab", 2) + 3;
    acc += (long)snprintf(buf, sizeof(buf), "p-%ld", acc);
    return acc;
}

int main(int, char**)
{
    // Fork BEFORE main does anything else, so as few PLT slots as possible are
    // resolved: the child is the first to hit the functions above.
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        printf("FAIL: fork: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        // CHILD: first-to-resolve a pile of PLT symbols in a COW address space.
        long r = child_exercise_plt();
        // Return a byte the parent can verify (non-zero => the whole chain of
        // lazy resolutions completed without faulting).
        _exit(r != 0 ? 42 : 7);
    }
    // PARENT: reap the child and report.
    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    if (w != pid) {
        printf("FAIL: waitpid returned %d (want %d): %s\n", (int)w, (int)pid, strerror(errno));
        return 1;
    }
    if (!WIFEXITED(status)) {
        printf("FAIL: child did not exit normally (status=0x%x) -- likely resolve_pltgot fault\n", status);
        return 1;
    }
    int code = WEXITSTATUS(status);
    if (code != 42) {
        printf("FAIL: child exit code %d (want 42) -- lazy PLT resolution in child broke\n", code);
        return 1;
    }
    // CROSS-AS COHERENCE: the child above resolved cross-object PLT symbols,
    // which inserts nodes into the resolving object's _used_by_resolve_plt_got
    // set.  If those nodes were allocated in the (now-reaped) child's COW arena,
    // the set's bucket chains now reference memory that only ever existed in
    // that dead address space.  Now the PARENT (AS0) resolves a DIFFERENT batch
    // of cross-object PLT symbols: the insert rehashes/relinks the same shared
    // set and dereferences the stale child-arena node -> the resolve_pltgot
    // NULL/COW-stale fault.  With the fix (set nodes on the identity heap) this
    // stays coherent and completes cleanly.
    long parent_acc = parent_exercise_plt();
    if (parent_acc == 0) {
        printf("FAIL: parent PLT exercise returned 0 (unexpected)\n");
        return 1;
    }

    // Second round: many children, each resolving + exiting, interleaved with
    // parent resolution, to stress the shared set across many COW arenas.
    for (int i = 0; i < 8; i++) {
        pid_t p2 = fork();
        if (p2 == 0) {
            long r = child_exercise_plt() + parent_exercise_plt();
            _exit(r != 0 ? 1 : 0);
        }
        int st = 0;
        waitpid(p2, &st, 0);
        if (!WIFEXITED(st)) {
            printf("FAIL: child %d faulted (status=0x%x) -- resolve_pltgot cross-AS fault\n", i, st);
            return 1;
        }
        // Parent touches the shared set again between children.
        (void)parent_exercise_plt();
    }

    printf("PASS: forked child resolved unresolved-in-parent PLT symbols cleanly\n");
    printf("PASS: parent + 8 reaped children resolved cross-object PLT with a coherent shared set\n");
    printf("SUMMARY: 0 failures\n");
    return 0;
}
