/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Objects a fork child loads are global: every process finds them in the
// module list.  So everything the load allocates for them must be readable
// from every address space, after the loading process is gone.
//  - "dlopen" (the default): a child dlopen()s a library and exits without
//    closing it.  The parent then walks the module list (dl_iterate_phdr,
//    reading each object's name and program headers), forks again, and the
//    new child walks it too.  8 rounds.
//  - "dlopen" also checks Linux visibility: the object a child loads is listed
//    in that child only, never in its parent or a sibling forked later.  Round
//    0 and 1 load and reap one child each; then 8 siblings each dlopen() and
//    call the library, all alive at once.
//  - "reap": a child dlopen()s the library, exits and is reaped; afterwards
//    no module in the kernel's list is the library, so nothing it loaded
//    outlives it.  200 rounds.
//  - "execve": 30 rounds of fork() + execve() of a payload, which loads the
//    payload in the exec'd thread; the parent reaps it and checks its exit
//    status.  It first checks that a fork child's own stack, on which
//    execve() loads the program, is at least the kernel default 64 KB.
//    Each exec runs in a new ELF namespace, and one boot has 31 free ones
//    unless the namespace of an exited program is reused.  A second argument
//    sets the number of rounds.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-dlopen requires CONF_fork\n");
    return 0;
}

#else

#include <osv/elf.hh>   // before <link.h>, as in libc/dlfcn.cc
#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *lib = "/tests/libforksym.so";

struct walk {
    int objects;
    int found;
    unsigned long sum;
};

static int visit(struct dl_phdr_info *info, size_t, void *data)
{
    auto w = static_cast<walk*>(data);
    w->objects++;
    if (info->dlpi_name && strstr(info->dlpi_name, "libforksym.so")) {
        w->found++;
    }
    for (int i = 0; i < info->dlpi_phnum; i++) {
        w->sum += info->dlpi_phdr[i].p_type + info->dlpi_phdr[i].p_memsz;
    }
    w->sum += info->dlpi_name ? strlen(info->dlpi_name) : 0;
    return 0;
}

static walk walk_modules()
{
    walk w{};
    dl_iterate_phdr(visit, &w);
    return w;
}

static int reap(pid_t c)
{
    int st = 0;
    if (c < 0 || waitpid(c, &st, 0) != c || !WIFEXITED(st)) {
        return -1;
    }
    return WEXITSTATUS(st);
}

// Objects in the kernel's module list named like the library, whatever
// address space loaded them.
static int listed_anywhere()
{
    int n = 0;
    elf::get_program()->with_modules([&](const elf::program::modules_list &ml) {
        for (auto obj : ml.objects) {
            if (obj->pathname().find("libforksym.so") != std::string::npos) {
                n++;
            }
        }
    });
    return n;
}

static int dlopen_and_call()
{
    void *h = dlopen(lib, RTLD_NOW);
    if (!h) {
        return 2;
    }
    auto fn = reinterpret_cast<long (*)(long)>(dlsym(h, "forksym_fn_1"));
    if (!fn || fn(20) != 41) {
        return 3;
    }
    // Listed here, once.
    return walk_modules().found == 1 ? 0 : 5;
}

static int run_dlopen()
{
    int failures = 0;
    for (int round = 0; round < 8; round++) {
        pid_t c = fork();
        if (c == 0) {
            _exit(dlopen_and_call());
        }
        int r = reap(c);
        if (r) {
            printf("FAIL: round %d: the loading child exited %d (2: dlopen, 3: dlsym/call, "
                   "5: not listed once)\n", round, r);
            failures++;
            break;
        }
        walk w = walk_modules();
        if (w.found) {
            printf("FAIL: round %d: the parent lists a library only its exited child loaded\n",
                   round);
            failures++;
            break;
        }
        pid_t g = fork();
        if (g == 0) {
            walk wg = walk_modules();
            _exit(wg.objects == w.objects && wg.sum == w.sum && !wg.found ? 0 : 4);
        }
        r = reap(g);
        if (r) {
            printf("FAIL: round %d: a later child's module walk differs (exit %d)\n", round, r);
            failures++;
            break;
        }
        if (round == 0) {
            printf("dlopen: %d objects in the parent, the library not listed\n", w.objects);
        }
    }
    if (failures) {
        return failures;
    }
    // Siblings alive at once, each loading the library for itself.
    const int n = 8;
    int pipes[n][2];
    pid_t kids[n];
    for (int i = 0; i < n; i++) {
        if (pipe(pipes[i])) {
            printf("FAIL: pipe\n");
            return 1;
        }
        kids[i] = fork();
        if (kids[i] == 0) {
            close(pipes[i][1]);
            int r = dlopen_and_call();
            char b;
            read(pipes[i][0], &b, 1);   // wait until every sibling has loaded
            if (!r && walk_modules().found != 1) {
                r = 6;
            }
            _exit(r);
        }
        close(pipes[i][0]);
    }
    for (int i = 0; i < n; i++) {
        write(pipes[i][1], "x", 1);
        close(pipes[i][1]);
    }
    int bad = 0;
    for (int i = 0; i < n; i++) {
        int r = reap(kids[i]);
        if (r) {
            printf("FAIL: sibling %d exited %d (6: listed other than once with siblings alive)\n",
                   i, r);
            bad++;
        }
    }
    printf("siblings: %d alive at once, %d failed\n", n, bad);
    if (walk_modules().found) {
        printf("FAIL: the parent lists the library after its siblings\n");
        bad++;
    }
    return failures + bad;
}

static int run_reap()
{
    int bad = 0, kept = 0;
    for (int round = 0; round < 200; round++) {
        pid_t c = fork();
        if (c == 0) {
            _exit(dlopen_and_call());
        }
        int r = reap(c);
        if (r) {
            printf("FAIL: round %d: the loading child exited %d\n", round, r);
            bad++;
            break;
        }
        int l = listed_anywhere();
        if (l) {
            kept = l;
            if (round < 3) {
                printf("FAIL: round %d: %d copies of the library stay in the module list "
                       "after its child was reaped\n", round, l);
            }
            bad++;
        }
    }
    printf("reap: 200 rounds, %d with the library still loaded (last count %d)\n", bad, kept);
    return bad != 0;
}

// execve() in a fork child loads the new program on the child thread's own
// stack (the app stack it runs on is the child's memory, which execve frees).
// Measured, that load needs about 16 KB, so give it the kernel default.
static const size_t exec_stack_min = 64 * 1024;

static int run_execve(int rounds)
{
    int bad = 0;
    pid_t c0 = fork();
    if (c0 == 0) {
        _exit(sched::thread::current()->get_stack_info().size >= exec_stack_min ? 0 : 1);
    }
    if (reap(c0)) {
        printf("FAIL: a fork child's own stack is under %zu bytes, too small for execve's load\n",
               exec_stack_min);
        bad++;
    }
    for (int i = 0; i < rounds; i++) {
        pid_t c = fork();
        if (c == 0) {
            char *const argv[] = { (char*)"/tests/payload-exit7.so", nullptr };
            char *const envp[] = { nullptr };
            execve(argv[0], argv, envp);
            _exit(99);
        }
        if (reap(c) != 7) {
            bad++;
        }
    }
    printf("execve: %d rounds, %d failed\n", rounds, bad);
    if (bad) {
        printf("FAIL: fork + execve\n");
    }
    return bad != 0;
}

int main(int argc, char **argv)
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
    const char *mode = argc > 1 ? argv[1] : "dlopen";
    int rounds = argc > 2 ? atoi(argv[2]) : 30;
    int failures = !strcmp(mode, "execve") ? run_execve(rounds)
                 : !strcmp(mode, "reap") ? run_reap() : run_dlopen();
    if (!failures) {
        printf("PASS: tst-fork-dlopen %s\n", mode);
    }
    printf("SUMMARY: %d failures\n", failures);
    return failures ? 1 : 0;
}

#endif
