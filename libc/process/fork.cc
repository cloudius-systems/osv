/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/fork.hh>
#include <osv/kernel_config_fork.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unordered_map>
#include <memory>
#include <osv/sched.hh>
#include <osv/mmu.hh>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/app.hh>
#include <osv/debug.hh>
#include <osv/kernel_config_fork.h>
#include <osv/fork_arena.hh>
#include "../libc.hh"


// Arch hook (arch/<arch>/fork.cc): create a child thread that resumes in
// fork()'s CALLER (at caller_ret, with caller_sp) on a private copy of the
// parent's user stack, returning 0 in the child.  Hands back the copied stack
// via out_stack_to_free so fork() can release it when the child is reaped.
extern sched::thread *fork_thread(void *caller_ret, void *caller_sp,
                                  void *resume_ctx, void **out_stack_to_free);

// atfork handler chains (defined in libc/pthread.cc).  glibc/musl register
// these internally; fork() must run prepare() in the parent before forking,
// parent() in the parent after, and child() in the child after.
extern "C" void __osv_run_atfork_prepare();
extern "C" void __osv_run_atfork_parent();
extern "C" void __osv_run_atfork_child();
namespace osv {
namespace fork {

namespace {

struct child_state {
    pid_t parent_pid;
    bool exited = false;
    int status = 0;              // encoded: (exit_code & 0xff) << 8, or signal
    shared_app_t execed_app;     // set if the child execve()'d a program
};

mutex g_lock;
condvar g_cv;
// child pid -> state
std::unordered_map<pid_t, std::shared_ptr<child_state>> g_children;

pid_t current_pid()
{
    return sched::thread::current()->id();
}

} // anonymous namespace

void adopt_execed_app(shared_app_t app)
{
    SCOPE_LOCK(g_lock);
    auto it = g_children.find(current_pid());
    if (it != g_children.end()) {
        it->second->execed_app = app;
    }
}

void register_child(pid_t child_pid, pid_t parent_pid)
{
    // The child registry is kernel-global bookkeeping shared across address
    // spaces (parent, child, reaper); keep its nodes in the identity heap, not
    // the COW fork arena.
    fork_arena::kernel_heap_scope kh;
    SCOPE_LOCK(g_lock);
    auto st = std::make_shared<child_state>();
    st->parent_pid = parent_pid;
    g_children[child_pid] = st;
}

void child_exited(pid_t child_pid, int status)
{
    pid_t parent;
    {
        SCOPE_LOCK(g_lock);
        auto it = g_children.find(child_pid);
        if (it == g_children.end()) {
            return;
        }
        if (it->second->exited) {
            return;   // already recorded (e.g. exit() then thread cleanup); don't clobber/re-notify
        }
        it->second->exited = true;
        it->second->status = status;
        parent = it->second->parent_pid;
        g_cv.wake_all();
    }
    // Notify the parent, Linux-style, that a child changed state.
    (void)parent;
    kill(getpid(), SIGCHLD);
}

bool exit_current_child(int status)
{
    pid_t me = current_pid();
    {
        SCOPE_LOCK(g_lock);
        if (g_children.find(me) == g_children.end()) {
            return false;   // top-level app: exit() shuts OSv down as usual
        }
    }
    // Encode like Linux wait status for a normal exit: WEXITSTATUS in bits 8-15.
    child_exited(me, (status & 0xff) << 8);
    return true;
}

pid_t wait_child(pid_t pid, int *status, int options)
{
    pid_t me = getpid();
    WITH_LOCK(g_lock) {
        while (true) {
            // Find a matching, exited child of the caller.
            for (auto it = g_children.begin(); it != g_children.end(); ++it) {
                bool match = (pid == -1 || pid == 0) ? (it->second->parent_pid == me)
                                                     : (it->first == pid);
                if (!match) {
                    continue;
                }
                if (it->second->exited) {
                    pid_t cpid = it->first;
                    if (status) {
                        *status = it->second->status;
                    }
                    g_children.erase(it);
                    return cpid;
                }
            }
            // No exited match.  Do we even have a matching (live) child?
            bool have_match = false;
            for (auto &kv : g_children) {
                if ((pid == -1 || pid == 0) ? (kv.second->parent_pid == me)
                                            : (kv.first == pid)) {
                    have_match = true;
                    break;
                }
            }
            if (!have_match) {
                errno = ECHILD;
                return -1;
            }
            if (options & WNOHANG) {
                return 0;   // matching child(ren) exist but none has exited yet
            }
            g_cv.wait(&g_lock);
        }
    }
    // not reached
    return -1;
}

} // namespace fork
} // namespace osv

using namespace osv;

extern "C"
__attribute__((noinline))
pid_t fork(void)
{
    // Capture the caller's full callee-saved register context FIRST, before
    // fork()'s body clobbers rbx/r12-r15.  At this point (right after fork's
    // prologue) those registers still hold the caller's values, and fork's rbp
    // frame gives us the caller's saved rbp ([rbp]), return address ([rbp+8]),
    // and post-return rsp (rbp+16).  The child resumes with exactly this
    // context so it continues in fork()'s caller as a normal `ret` would --
    // essential for deep call chains (see osv::fork_resume_ctx, tst-fork-deep).
    fork_resume_ctx ctx;
    void **fp = static_cast<void**>(__builtin_frame_address(0));
    asm volatile("movq %%rbx, %0\n\t"
                 "movq %%r12, %1\n\t"
                 "movq %%r13, %2\n\t"
                 "movq %%r14, %3\n\t"
                 "movq %%r15, %4\n\t"
                 : "=m"(ctx.rbx), "=m"(ctx.r12), "=m"(ctx.r13),
                   "=m"(ctx.r14), "=m"(ctx.r15));
    ctx.rbp = reinterpret_cast<unsigned long>(fp[0]);          // caller's rbp
    ctx.rip = reinterpret_cast<unsigned long>(__builtin_return_address(0));
    ctx.rsp = reinterpret_cast<unsigned long>(fp + 2);         // fork rbp + 16

    pid_t parent = getpid();

    void *caller_ret = reinterpret_cast<void*>(ctx.rip);
    void *caller_sp  = reinterpret_cast<void*>(ctx.rsp);

    void *stack_to_free = nullptr;
    // POSIX: run pthread_atfork prepare handlers in the parent before forking.
    __osv_run_atfork_prepare();
    sched::thread *child = fork_thread(caller_ret, caller_sp, &ctx, &stack_to_free);
    if (!child) {
        __osv_run_atfork_parent();  // undo prepare-side locking
        errno = ENOMEM;
        return -1;
    }
    pid_t cpid = child->id();

    // Stage 2 fork: give the child its OWN address space, a COW clone of the
    // parent's.  Private writable mappings become copy-on-write in both parent
    // and child; the kernel half of the page table is shared.  The child
    // thread runs in this address space (its CR3 is loaded on context switch).
    mmu::address_space *child_as =
        mmu::clone_address_space(sched::thread::current()->address_space());
    child->set_address_space(child_as);

    // Register the child BEFORE starting it so a fast child->exit cannot race
    // ahead of the parent's bookkeeping.
    fork::register_child(cpid, parent);

    // The cleanup closure (a std::function, heap-allocated) is stored in the
    // thread object and invoked by the reaper (a kernel thread, AS0); keep it
    // in the identity heap, not the parent's COW arena.
    fork_arena::kernel_heap_scope kh_cleanup;
    // Single cleanup, run by the thread reaper once the (detached) child has
    // fully terminated: free the copied user stack; record a default status if
    // the child fell off the end without exit() (real codes are recorded by
    // exit()/execve() via fork::child_exited() before this runs); destroy the
    // child's copy-on-write address space; and dispose the child thread object.
    // Disposing is essential: the child thread holds a shared_ptr to its
    // application_runtime (set at thread construction), and only destroying the
    // thread releases it; without it the app runtime never drops to zero,
    // ~application_runtime never fires, application::join() blocks forever on
    // _terminated, and OSv hangs at shutdown instead of powering off.
    child->set_cleanup([cpid, stack_to_free, child, child_as] {
        fork::child_exited(cpid, 0);
        if (stack_to_free) {
            free(stack_to_free);
        }
        // Only destroy the child address space if the child still owns it.
        // execve() replaces the child AS with the global (kernel) AS and
        // destroys child_as itself; destroying it again here is a double-free
        // (GP fault / ~rwlock assert when the reaper tears down freed vmas).
        if (child->address_space() == child_as) {
            mmu::destroy_address_space(child_as);
        }
        sched::thread::dispose(child);
    });

    child->start();

    // POSIX: run atfork parent handlers in the parent after the fork.  (The
    // child runs its atfork child handlers in its own context, in the arch
    // fork_thread trampoline, before resuming user code.)
    __osv_run_atfork_parent();

    // Parent path: return the child's pid.  (The child resumes in fork()'s
    // caller with return value 0, on its private stack.)
    return cpid;
}

extern "C"
pid_t vfork(void)
{
    // On OSv the child already shares the parent's address space (the classic
    // vfork contract of "child borrows the parent's memory until exec/_exit")
    // is actually served more faithfully than fork's copy semantics.  Map to
    // fork(); the shared-memory behavior matches vfork's documented contract.
    // fork() is ::fork() here (an unqualified `fork` would resolve to the
    // osv::fork namespace brought in by `using namespace osv`).
    return ::fork();
}
