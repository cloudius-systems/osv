/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Free kernel memory on one CPU that another CPU allocated, after a fork().
// A cross-CPU free pushes the object on a per-CPU-pair queue with preemption
// disabled.  If those queues were mapped copy-on-write by fork(), the push
// takes a write fault with preemption off and the guest asserts in
// page_fault().  The queue array grows as CPUs squared and reached the
// fork-cloned address range at about 105 CPUs, so run this with -smp 105 or
// more to cover that case; with fewer CPUs it only checks the plain path.

#include <osv/kernel_config_fork.h>

#include <cstdio>

#if !CONF_fork

int main()
{
    printf("SKIP: tst-fork-xcpu-free requires CONF_fork\n");
    return 0;
}

#else

#include <osv/sched.hh>
#include <osv/fork_arena.hh>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <vector>

// Allocate on cpu `from` and free on cpu `to`, from kernel-heap allocations
// (as the network and ZFS threads do) so they come from the per-CPU pools.
static void cross_free(unsigned from, unsigned to, int n)
{
    std::vector<void*> objs(n);
    sched::thread::pin(sched::cpus[from]);
    {
        fork_arena::kernel_heap_scope kh;
        for (auto &p : objs) {
            p = malloc(64);
        }
    }
    sched::thread::pin(sched::cpus[to]);
    for (auto p : objs) {
        free(p);
    }
}

int main()
{
    unsigned ncpus = sched::cpus.size();
    if (ncpus < 2) {
        printf("SKIP: needs at least 2 CPUs\n");
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        printf("FAIL: fork\n");
        return 1;
    }
    if (pid == 0) {
        for (unsigned c = 0; c < ncpus; c++) {
            cross_free(c, (c + 1) % ncpus, 300);
        }
        _exit(0);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status)) {
        printf("FAIL: child status 0x%x\n", status);
        return 1;
    }
    for (unsigned c = 0; c < ncpus; c++) {
        cross_free((c + 1) % ncpus, c, 300);
    }
    printf("%u cpus: cross-CPU frees after fork done\nPASS\n", ncpus);
    return 0;
}

#endif
