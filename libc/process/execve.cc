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

    osv::shared_app_t child;
    // execve() replaces the address space entirely: the forked child's COW
    // private memory is discarded.  Move this thread back to the kernel (global)
    // address space BEFORE launching the new program, so the new program's
    // mappings go into the global vma_list + page table (consistent with
    // get_root_pt) rather than the now-defunct child COW address space.  The
    // freshly created app threads inherit this thread's (now global) AS.
    {
        auto self = sched::thread::current();
        auto old_as = self->address_space();
        if (old_as != mmu::kernel_address_space()) {
            self->set_address_space(mmu::kernel_address_space());
            mmu::switch_to_runtime_page_tables();  // reload CR3 = global root
            mmu::destroy_address_space(old_as);
        }
    }
    try {
        // new_program=true => fresh ELF namespace, so the exec'd program gets
        // its own globals rather than colliding with the caller's.
        child = osv::application::run(path, args, true,
                                      envp ? &env : nullptr);
    } catch (const osv::launch_error &e) {
        // Could not load/exec the target - Linux returns ENOENT/ENOEXEC/EACCES.
        return libc_error(ENOENT);
    }

    // Record the exec'd app so the fork/wait layer can reap it under the pid
    // of the thread that called execve() (Linux keeps the pid across exec).
    osv::fork::adopt_execed_app(child);

    // A successful execve() does not return to the caller.  Join the child app
    // (run it to completion on this thread) and then exit with its return code,
    // so this thread never re-enters the old program.
    int rc = child->join();
    _exit(rc);
    // not reached
    return 0;
}
#endif // CONF_fork
