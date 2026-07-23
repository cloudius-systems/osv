/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <unistd.h>
#include <errno.h>
#include <osv/kernel_config_fork.h>

#if !CONF_fork
// Without fork() support, execve() keeps its historical stub behavior.
#include <osv/stubbing.hh>
#include "../libc.hh"
int execve(const char *path, char *const argv[], char *const envp[])
{
    WARN_STUBBED();
    return libc_error(ENOEXEC);
}
#else
#include <string>
#include <vector>
#include <unordered_map>
#include <osv/app.hh>
#include <osv/stubbing.hh>
#include "../libc.hh"
#include <osv/fork.hh>
#include <osv/mmu.hh>
#include <osv/sched.hh>

// execve() on OSv.
//
// OSv is a single-address-space unikernel: there is no separate process image
// to replace.  We approximate execve() by launching the requested program as a
// new OSv "application" in a fresh ELF namespace (its own set of globals, the
// closest OSv has to a fresh address space) via osv::application::run(), and
// then ending the calling thread so it does not return to the old program --
// matching the Linux contract that a successful execve() never returns.
//
// This makes the common fork()+execve() idiom work: the fork child calls
// execve(), which starts the new program and the child thread becomes that
// program's driver.  The parent is unaffected (it is a different thread in the
// same address space) and can waitpid() on the child.
//
// Limitations (documented in documentation/fork.md): the old program's global
// state is not torn down the way a real address-space replacement would; the
// new program runs in its own ELF namespace but shares the kernel heap.

// Continuation of execve() that runs the exec'd program to completion.  It is
// invoked either directly (normal case) or on a fresh stack after we have left
// the same-VA fork stack (fork-child case); either way it never returns -- a
// successful exec ends the thread with the program's exit code.
static void execve_run_and_exit(const std::string *path,
                                const std::vector<std::string> *args,
                                const std::unordered_map<std::string,std::string> *env,
                                bool have_env)
{
    osv::shared_app_t child;
    try {
        // new_program=true => fresh ELF namespace, so the exec'd program gets
        // its own globals rather than colliding with the caller's.
        child = osv::application::run(*path, *args, true, have_env ? env : nullptr);
    } catch (const osv::launch_error &e) {
        // Could not load/exec the target - Linux returns ENOENT/ENOEXEC/EACCES.
        errno = ENOENT;
        _exit(127);
    }
    // Record the exec'd app so the fork/wait layer can reap it under the pid of
    // the thread that called execve() (Linux keeps the pid across exec).
    osv::fork::adopt_execed_app(child);
    // A successful execve() does not return to the caller: run the program to
    // completion on this thread and exit with its return code.
    int rc = child->join();
    _exit(rc);
}

extern "C"
int execve(const char *path, char *const argv[], char *const envp[])
{
    if (!path || !argv) {
        return libc_error(EFAULT);
    }

    std::vector<std::string> args;
    for (char *const *a = argv; *a; a++) {
        args.push_back(*a);
    }
    if (args.empty()) {
        // Linux requires argv to have at least argv[0]; be lenient and use path.
        args.push_back(path);
    }

    std::unordered_map<std::string, std::string> env;
    if (envp) {
        for (char *const *e = envp; *e; e++) {
            std::string kv(*e);
            auto eq = kv.find('=');
            if (eq != std::string::npos) {
                env[kv.substr(0, eq)] = kv.substr(eq + 1);
            }
        }
    }

    // execve() replaces the caller's program: the exec'd target must run in the
    // GLOBAL (AS0) address space, not the forked child's COW address space, so
    // its ELF mappings and demand-paged I/O behave normally (a fault during
    // block-I/O completion in a COW child AS is illegal -- assert(preemptable)).
    // But with same-VA fork stacks, THIS thread is currently executing on its
    // app stack whose pages are PRIVATE to the child AS at VAs it shares with
    // the (possibly blocked) parent.  Switching CR3 to AS0 while on that stack
    // would alias the stack onto the parent's physical page and destroy_address
    // _space() would free the running stack.  So we first move this thread onto
    // its own kernel stack (allocated in the global heap, valid in every AS),
    // THEN switch to AS0, tear down the child AS, and run the target.
    //
    // Because that stack/AS switch is a point of no return (we cannot return -1
    // to the caller afterwards), first verify the target is loadable so a
    // failed exec still returns to the caller (which typically _exit()s its own
    // fallback status) instead of stranding the thread.
    std::string spath(path);
    auto self = sched::thread::current();
    auto old_as = self->address_space();
    if (old_as && old_as != mmu::kernel_address_space()) {
        if (::access(path, F_OK) != 0) {
            return libc_error(ENOENT);   // caller decides the fallback
        }
        auto si = self->get_stack_info();   // the thread's own kernel stack
        void *kstack_top = static_cast<char*>(si.begin) + si.size;
        // 16-byte align and leave a little headroom.
        uintptr_t sp = (reinterpret_cast<uintptr_t>(kstack_top) & ~uintptr_t(15)) - 256;
        // Package the continuation args so the new stack frame can reach them.
        static thread_local std::string s_path;
        static thread_local std::vector<std::string> s_args;
        static thread_local std::unordered_map<std::string,std::string> s_env;
        static thread_local bool s_have_env;
        static thread_local mmu::address_space *s_old_as;
        // The continuation args must OUTLIVE the child address-space teardown
        // below.  A std::string / vector / map keeps its ELEMENT storage on the
        // heap; assigned here (in the forked child, an app thread) that storage
        // routes to the COW fork arena -- which lives in the child AS we are
        // about to destroy_address_space().  After the teardown s_path's char
        // buffer would be unmapped, so execve_run_and_exit() would read a zeroed
        // path ("executable too short /") and the exec would spuriously fail.
        // Force the identity kernel heap for these so they survive the teardown.
        {
            fork_arena::kernel_heap_scope kh;
            s_path = spath; s_args = args; s_env = env;
            s_have_env = (envp != nullptr);
            s_old_as = old_as;
        }
        // Switch to AS0 and tear down the child AS -- but do it from the kernel
        // stack, so freeing the child's app-stack pages cannot pull the rug.
        // We accomplish the ordering with an rsp switch: move to the kernel
        // stack, then call a lambda that switches CR3, destroys old_as, and
        // runs the program.  It never returns.
        self->set_address_space(mmu::kernel_address_space());
        asm volatile(
            "movq %0, %%rsp \n\t"
            "movq %0, %%rbp \n\t"
            "call *%1 \n\t"
            : : "r"(sp), "r"(reinterpret_cast<void*>(+[]{
                    // Now on the kernel stack (valid in every AS) with this
                    // thread reassigned to AS0; reload CR3 to AS0, then destroy
                    // the child AS HERE (exactly once): we have left the child's
                    // same-VA app stack, so freeing its page tables is safe.
                    // The reap-time cleanup (libc/process/fork.cc) sees the
                    // thread's AS is now AS0 (!= child_as) and skips its own
                    // destroy, so child_as is destroyed once whether the child
                    // exits normally (cleanup destroys) or execs (here).  Never
                    // returns.
                    mmu::switch_to_runtime_page_tables();
                    mmu::destroy_address_space(s_old_as);
                    execve_run_and_exit(&s_path, &s_args, &s_env, s_have_env);
                }))
            : "memory");
        __builtin_unreachable();
    }

    // Not a fork child (or already in AS0): run the target directly.
    osv::shared_app_t child;
    try {
        // new_program=true => fresh ELF namespace, so the exec'd program gets
        // its own globals rather than colliding with the caller's.
        child = osv::application::run(path, args, true,
                                      envp ? &env : nullptr);
    } catch (const osv::launch_error &e) {
        // Could not load/exec the target - Linux returns ENOENT/ENOEXEC/EACCES.
        return libc_error(ENOENT);
    }
    osv::fork::adopt_execed_app(child);
    int rc = child->join();
    _exit(rc);
    // not reached
    return 0;
}
#endif // CONF_fork
