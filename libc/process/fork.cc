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
#include <unordered_set>
#include <vector>
#include <memory>
#include <osv/sched.hh>
#include <osv/mmu.hh>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/app.hh>
#include <osv/debug.hh>
#include <osv/file.h>
#include <osv/kernel_config_fork.h>
#include <osv/fork_arena.hh>
#include <osv/pid.h>
#include "../libc.hh"
#include "../signal.hh"   // nsignals, signal_actions (per-child sig table)


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

// Provided by fs/vfs/kern_descrip.cc: snapshot the open fds and fhold each,
// emitting (fd, file*) pairs.  Used to give a fork child its own inherited-fd
// references over OSv's single shared global fd table.
extern "C" void fork_snapshot_open_fds(
    void (*emit)(void *ctx, int fd, struct file *fp), void *ctx);

// Provided by fs/vfs/kern_descrip.cc: null gfdt[fd] iff it currently holds @fp
// (under gfdt_lock).  Used to release a shared fd slot once its last inheriting
// fork child is done with it, so the fd can be reused.
extern "C" void fork_clear_gfdt_slot_if(int fd, struct file *fp);

// Provided by fs/vfs/kern_descrip.cc: close an fd an exited fork child opened
// for ITSELF after fork and never closed (null the shared gfdt slot + fdrop).
// Called by the reaper (release_inherited_fds) so these private fds do not leak
// in the shared table across backend churn.  Closes ONLY if gfdt[fd] still holds
// @fp (identity-keyed: a dup2 onto the same number after the child exited leaves
// a different file there, which the reaper must not touch).
extern "C" void fork_reap_close_child_opened_fd(int fd, struct file *fp);

// Provided by libc/signal.cc: drop a process's interval timers.
extern "C" void fork_release_process_itimers(mmu::address_space *as);


namespace osv {
namespace fork {

namespace {

struct child_state {
    pid_t parent_pid;
    // Waitable: set once the child's resources are released (see fork()'s
    // cleanup), so the parent's wait returns after them, as on Linux.
    bool exited = false;
    bool status_recorded = false; // exit() recorded the status below
    int status = 0;              // encoded: (exit_code & 0xff) << 8, or signal
};

mutex g_lock;
condvar g_cv;
// child pid -> state
std::unordered_map<pid_t, std::shared_ptr<child_state>> g_children;

// ---- fork per-process pid identity ---------------------------------------
//
// OSv normally reports one pid (OSV_PID) for the whole unikernel.  That breaks
// any forking app that identifies its "processes" by pid -- most importantly
// PostgreSQL's latch layer: every backend sets MyProcPid = getpid(), and
// SetLatch(other_backend_latch) compares the latch's owner_pid to MyProcPid to
// decide LOCAL wakeup (own self-pipe) vs CROSS-process wakeup
// (kill(owner_pid, SIGURG) -> the target's SIGURG handler pokes ITS self-pipe).
// With one shared pid every backend sees owner_pid == MyProcPid, so a
// cross-backend SetLatch takes the LOCAL path and pokes the caller's own pipe
// -- the waiting backend is never woken and a lock wait wedges forever (0 tps,
// no failure).  So each fork child gets a DISTINCT pid (its child thread id,
// exactly what fork() returned to the parent), keyed by its address space; the
// top-level app keeps OSV_PID.  Kept on the identity kernel heap (cross-AS
// bookkeeping walked by the parent, children and the SIGURG kill router).
mutex g_pid_lock;
std::unordered_map<mmu::address_space *, pid_t> g_as_pid;   // child AS  -> pid
std::unordered_map<pid_t, mmu::address_space *> g_pid_as;   // child pid -> AS
// The child's thread, for kill() to interrupt, and how many kill()s hold it.
struct pid_thread {
    sched::thread *t;
    unsigned holds;
};
std::unordered_map<pid_t, pid_thread> g_pid_thread;
condvar g_pid_thread_cv;

// ---- fork fd-inheritance -------------------------------------------------
//
// OSv has ONE GLOBAL fd table (fs/vfs/kern_descrip.cc gfdt[]) shared by a fork
// parent and child.  A POSIX child gets its own fd table; here we approximate
// that by giving the child its OWN reference (fhold) on every open file it
// inherited at fork time, and tracking WHICH fds it inherited (keyed by the
// child's address_space, which uniquely identifies a fork child).  The child's
// close() of an inherited fd then drops only the child's reference and leaves
// the shared gfdt slot intact for the parent; fds the child opens itself after
// fork are NOT in this set and close normally.  On child teardown any still-
// held inherited references are dropped.  All of this lives on the identity
// kernel heap (kernel-global bookkeeping shared across parent/child/reaper).
mutex g_fd_lock;
// child address_space -> { inherited fd -> file* we hold a ref on }
std::unordered_map<mmu::address_space *,
                   std::unordered_map<int, struct file *>> g_inherited_fds;
// child address_space -> set of fds this child allocated ITSELF after fork.
// OSv has one shared global fd table, so a child fdclose() must be able to tell
// "an fd I opened after fork" (close it normally, null the slot) from "a fd the
// parent owns that I merely have a numeric handle to" (must NOT null the shared
// slot -- that would tear the fd out from under the parent).  PostgreSQL's
// ClosePostmasterPorts() close()s the postmaster's death-watch pipe fd in every
// backend; that fd is the PARENT's, was created after this child's fork
// snapshot, and closing it the normal way nulled gfdt[fd] and corrupted the
// postmaster.  Tracking child-opened fds lets a child's close of a
// not-inherited, not-child-opened fd be a harmless no-op on the shared slot.
// (fd, file*) pairs the top-level OWNER has close()d while a live child still
// inherited them: the owner relinquished the shared gfdt slot to the child(ren)
// but left it pointing at the file (OSv single fd table -- the child looks it
// up via gfdt[fd]).  The LAST inheriting child to close/teardown such an fd
// clears the slot.  A slot NOT in this set is still owned by the top-level app,
// so a child closing its inherited copy must never clear it.
std::unordered_map<int, struct file *> g_owner_released_fds;
// Value is the file* the child installed at that fd, so reap closes the slot
// ONLY if it still holds that exact file -- never a file a parent/sibling dup2'd
// onto the same number after the child exited (identity-keyed, not number-keyed).
std::unordered_map<mmu::address_space *, std::unordered_map<int, struct file *>> g_child_opened_fds;

// ---- fork per-child signal dispositions ----------------------------------
//
// POSIX signal dispositions are per-process, but OSv keeps a single global
// signal_actions[] (libc/signal.cc).  A fork child that resets its inherited
// handlers (e.g. PostgreSQL's startup process) would clobber the parent
// postmaster's handlers -- notably the SIGCHLD reaper the postmaster needs to
// reap children and reach PM_RUN.  Give each fork-child address space its own
// copy of the table, initialized from the parent's at fork time.  Kept on the
// identity kernel heap like the other cross-AS fork bookkeeping.
mutex g_sig_lock;
struct child_sigtable {
    struct sigaction actions[nsignals];
};
std::unordered_map<mmu::address_space *, child_sigtable *> g_child_sigtables;

pid_t current_pid()
{
    return sched::thread::current()->id();
}

// The current thread's fork-child address space, or nullptr if this is the
// kernel / top-level (non-fork) application (which owns the fds outright and
// must close them the normal way).
mmu::address_space *current_child_as()
{
    auto *as = mmu::current_address_space();
    if (!as || as == mmu::kernel_address_space()) {
        return nullptr;
    }
    return as;
}

} // anonymous namespace

// Snapshot callback: record the inherited fd and the ref taken on its file.
static void inherit_one_fd(void *ctx, int fd, struct file *fp)
{
    auto *m = static_cast<std::unordered_map<int, struct file *> *>(ctx);
    (*m)[fd] = fp;
}

// Take the child's own reference on every fd currently open in the (shared)
// global fd table.  Called from fork() with @child_as, on the identity heap.
static void inherit_open_fds(mmu::address_space *child_as)
{
    fork_arena::kernel_heap_scope kh;
    std::unordered_map<int, struct file *> held;
    fork_snapshot_open_fds(&inherit_one_fd, &held);
    std::vector<struct file *> not_ours;
    {
        SCOPE_LOCK(g_fd_lock);
        // A slot the owner has already closed is kept only for the children
        // that inherited it (a connection socket a forking server handed to a
        // running child). Unless the forking process is one of those children,
        // the slot is not open in it, so the new child must not inherit it. If
        // it did, every child forked while any such slot is alive would keep
        // that slot alive, and with overlapping children the slots would never
        // be freed.
        mmu::address_space *parent_as = current_child_as();
        auto pit = parent_as ? g_inherited_fds.find(parent_as) : g_inherited_fds.end();
        for (auto it = held.begin(); it != held.end();) {
            auto orel = g_owner_released_fds.find(it->first);
            bool parent_holds = false;
            if (pit != g_inherited_fds.end()) {
                auto h = pit->second.find(it->first);
                parent_holds = h != pit->second.end() && h->second == it->second;
            }
            if (orel != g_owner_released_fds.end() && orel->second == it->second &&
                    !parent_holds) {
                not_ours.push_back(it->second);
                it = held.erase(it);
            } else {
                ++it;
            }
        }
        g_inherited_fds[child_as] = std::move(held);
    }
    for (auto *fp : not_ours) {
        fdrop(fp);
    }
}

// Drop every remaining inherited reference held by @child_as.  Called on child
// teardown (from the reaper) and after execve() replaces the child image.
static void release_inherited_fds(mmu::address_space *child_as)
{
    std::unordered_map<int, struct file *> held;
    std::unordered_map<int, struct file *> to_clear;
    std::unordered_map<int, struct file *> opened;   // fd -> file* this child opened after fork
    {
        SCOPE_LOCK(g_fd_lock);
        auto it = g_inherited_fds.find(child_as);
        if (it == g_inherited_fds.end()) {
            // Even with no inherited fds, a child may have opened its own fds
            // after fork; still reclaim those below.
            auto oit = g_child_opened_fds.find(child_as);
            if (oit != g_child_opened_fds.end()) {
                opened = std::move(oit->second);
                g_child_opened_fds.erase(oit);
            }
            if (opened.empty()) {
                return;
            }
            // fall through to close the child-opened fds (held/to_clear empty)
        } else {
            held = std::move(it->second);
            g_inherited_fds.erase(it);
            auto oit = g_child_opened_fds.find(child_as);
            if (oit != g_child_opened_fds.end()) {
                opened = std::move(oit->second);
                g_child_opened_fds.erase(oit);
            }
            // For any fd this child was the LAST live inheritor of AND that the
            // top-level owner already relinquished (g_owner_released_fds), collect
            // it to clear the slot AFTER releasing g_fd_lock (gfdt_lock must not
            // nest inside g_fd_lock).  A slot the owner still holds is left intact.
            // Evaluated against the REMAINING children.
            for (auto &kv : held) {
                auto orel = g_owner_released_fds.find(kv.first);
                if (orel == g_owner_released_fds.end() || orel->second != kv.second) {
                    continue;   // owner still holds this slot: leave it
                }
                bool other_holds = false;
                for (auto &c : g_inherited_fds) {
                    auto o = c.second.find(kv.first);
                    if (o != c.second.end() && o->second == kv.second) {
                        other_holds = true;
                        break;
                    }
                }
                if (!other_holds) {
                    to_clear[kv.first] = kv.second;
                    g_owner_released_fds.erase(orel);
                }
            }
        }
    }
    for (auto &kv : to_clear) {
        fork_clear_gfdt_slot_if(kv.first, kv.second);
    }
    for (auto &kv : held) {
        fdrop(kv.second);
    }
    // Close the fds this backend opened ITSELF after fork.  OSv keeps one shared
    // global fd table (gfdt); a reaped backend used to leave these installed --
    // on real Linux the whole fd table is closed at process exit, but here the
    // reaper only dropped INHERITED refs and merely forgot the child-opened
    // set.  That leaks a handful of fds per backend into gfdt, and across
    // sustained connection/backend churn (or autovacuum's per-cycle worker
    // forks) gfdt fills to FDMAX -> pipe()/accept() fail with EMFILE and the
    // guest wedges (reboot-resettable).  Close each here exactly as the
    // backend's own close() would: null the shared slot iff it still points at
    // that file, then drop the reference.  These fds are private to the exited
    // backend (opened after its fork snapshot), so nulling the slot cannot tear
    // an fd out from under the parent or a sibling.
    for (auto &kv : opened) {
        fork_reap_close_child_opened_fd(kv.first, kv.second);
    }
}

// Called by fdclose() (fs/vfs/kern_descrip.cc) for every close().  If the
// current thread is a fork child and @fd is one it inherited, drop only the
// child's inherited reference (leaving the shared gfdt slot and the parent's
// reference intact) and report the close handled.  Otherwise return false so
// the normal fdclose() path runs (top-level app, or a child closing an fd it
// opened itself after fork).
// Called by _fdalloc() (fs/vfs/kern_descrip.cc) after a fork child allocates a
// new fd for itself.  Records it so the child's later close() of it nulls the
// shared slot normally (it is the child's own fd), distinct from a numeric
// handle to one of the parent's fds.
extern "C" void fork_child_opened_fd(int fd, struct file *fp)
{
    mmu::address_space *child_as = current_child_as();
    if (!child_as) {
        return;
    }
    fork_arena::kernel_heap_scope kh;
    SCOPE_LOCK(g_fd_lock);
    // Record the file* the caller just installed at this fd (passed in, not
    // looked up -- looking it up under gfdt_lock here would nest gfdt_lock
    // inside g_fd_lock and invert the order fork_owner_close_inherited_fd uses,
    // a deadlock).  The reaper later closes the slot only if it STILL holds
    // this exact file, so a dup2 onto the same number after the child exits is
    // never torn out.
    g_child_opened_fds[child_as][fd] = fp;
}

// Called by fdclose() (fs/vfs/kern_descrip.cc) for a fork child closing an fd
// that is NOT one it inherited.  Returns true iff the child opened this fd
// ITSELF after fork (so fdclose should proceed to null the shared slot);
// returns false if the fd belongs to the parent (single shared table) and the
// child must NOT touch the shared slot -- fdclose then treats the close as a
// harmless no-op instead of tearing the parent's fd out of gfdt[].
extern "C" bool fork_child_owns_fd(int fd)
{
    mmu::address_space *child_as = current_child_as();
    if (!child_as) {
        return true;   // not a child: normal close
    }
    SCOPE_LOCK(g_fd_lock);
    auto it = g_child_opened_fds.find(child_as);
    if (it == g_child_opened_fds.end()) {
        return false;
    }
    auto fit = it->second.find(fd);
    if (fit == it->second.end()) {
        return false;
    }
    it->second.erase(fit);
    return true;
}

extern "C" bool fork_child_close_inherited_fd(int fd)
{
    mmu::address_space *child_as = current_child_as();
    if (!child_as) {
        return false;   // kernel / top-level app: normal close
    }
    struct file *fp = nullptr;
    bool clear_slot = false;
    {
        SCOPE_LOCK(g_fd_lock);
        auto it = g_inherited_fds.find(child_as);
        if (it == g_inherited_fds.end()) {
            return false;
        }
        auto fit = it->second.find(fd);
        if (fit == it->second.end()) {
            return false;   // fd the child opened itself after fork: normal close
        }
        fp = fit->second;
        it->second.erase(fit);
        // Clear the shared gfdt slot only if (1) the top-level OWNER already
        // relinquished this fd to the children (fork_owner_close_inherited_fd
        // recorded it), and (2) NO OTHER live child still inherits this exact
        // fd->file.  Otherwise the parent (or another child) still needs the
        // slot, so leave it.  Cleared below, after releasing g_fd_lock
        // (gfdt_lock must not nest inside g_fd_lock: fdclose() takes gfdt_lock
        // then g_fd_lock).
        auto orel = g_owner_released_fds.find(fd);
        bool owner_released = (orel != g_owner_released_fds.end() &&
                               orel->second == fp);
        if (owner_released) {
            clear_slot = true;
            for (auto &kv : g_inherited_fds) {
                if (kv.first == child_as) {
                    continue;
                }
                auto o = kv.second.find(fd);
                if (o != kv.second.end() && o->second == fp) {
                    clear_slot = false;
                    break;
                }
            }
            if (clear_slot) {
                g_owner_released_fds.erase(orel);
            }
        }
    }
    if (clear_slot) {
        fork_clear_gfdt_slot_if(fd, fp);
    }
    fdrop(fp);   // drop only the child's inherited reference
    return true;
}

// Called by fdclose() (fs/vfs/kern_descrip.cc) from a NON-child context (the
// top-level app / postmaster / kernel) when it closes an fd.  If some LIVE fork
// child currently inherits THIS fd holding THIS same file, the shared gfdt slot
// must stay pointing at that file: OSv has a single shared fd table, so the
// child looks the connection up via gfdt[fd] -- nulling the slot (or letting it
// be reused by a later fdalloc) is exactly what makes the child's getsockname()
// fail (ENOTSOCK when the slot got a vnode, EBADF when still null).  This is
// PostgreSQL's postmaster->backend handoff: accept() -> gfdt[N]=socket, fork()
// (child inherits N), then the postmaster closesocket(N).  Returns true to tell
// fdclose() to LEAVE gfdt[fd] intact (it still fdrop()s the owner's reference);
// the inheriting child keeps its own ref and, on its own close/teardown,
// releases it and clears the slot.  Returns false (normal close: null the slot
// + fdrop) when no live child inherits @fd.
extern "C" bool fork_owner_close_inherited_fd(int fd, struct file *fp)
{
    // Only the top-level owner (not a fork child) hands its slot to a child
    // this way; a child closing its own inherited fd goes through
    // fork_child_close_inherited_fd above.
    if (current_child_as()) {
        return false;
    }
    SCOPE_LOCK(g_fd_lock);
    for (auto &kv : g_inherited_fds) {
        auto fit = kv.second.find(fd);
        if (fit != kv.second.end() && fit->second == fp) {
            // A live child inherits this exact fd->file: keep the shared slot
            // and record that the owner has relinquished it, so the last
            // inheriting child to go clears the slot.  Kernel-global
            // bookkeeping shared across parent/child/reaper -> identity heap.
            fork_arena::kernel_heap_scope kh;
            g_owner_released_fds[fd] = fp;
            return true;
        }
    }
    return false;
}

// ---- fork per-child signal dispositions (see g_child_sigtables) -----------

// The current fork child's private signal_actions[], or nullptr for the
// top-level app / kernel (which uses the global table).  Called by
// libc/signal.cc's current_signal_actions().
extern "C" struct sigaction *fork_child_signal_actions()
{
    mmu::address_space *child_as = current_child_as();
    if (!child_as) {
        return nullptr;
    }
    SCOPE_LOCK(g_sig_lock);
    auto it = g_child_sigtables.find(child_as);
    if (it == g_child_sigtables.end()) {
        return nullptr;
    }
    return it->second->actions;
}

// The signal_actions[] table of a SPECIFIC fork-child address space (nullptr
// if @child_as has none / is the top-level app).  Used by kill() to read the
// TARGET backend's handler when routing a cross-backend signal (the SIGURG
// that wakes another backend's latch) into that backend's context.
extern "C" struct sigaction *fork_child_signal_actions_for(mmu::address_space *child_as)
{
    if (!child_as || child_as == mmu::kernel_address_space()) {
        return nullptr;
    }
    SCOPE_LOCK(g_sig_lock);
    auto it = g_child_sigtables.find(child_as);
    if (it == g_child_sigtables.end()) {
        return nullptr;
    }
    return it->second->actions;
}

// Give @child_as its own copy of the signal dispositions, seeded from the
// parent's table so the child inherits them (POSIX).  Called from fork().
extern "C" void fork_init_child_signal_actions(mmu::address_space *child_as,
                                    const struct sigaction *parent_table)
{
    fork_arena::kernel_heap_scope kh;
    auto *tbl = new child_sigtable();
    for (unsigned i = 0; i < nsignals; ++i) {
        tbl->actions[i] = parent_table[i];
    }
    SCOPE_LOCK(g_sig_lock);
    auto &slot = g_child_sigtables[child_as];
    if (slot) {
        delete slot;   // shouldn't happen, but never leak
    }
    slot = tbl;
}

// Drop @child_as's private signal table on child teardown / execve.
extern "C" void fork_release_child_signal_actions(mmu::address_space *child_as)
{
    child_sigtable *tbl = nullptr;
    {
        SCOPE_LOCK(g_sig_lock);
        auto it = g_child_sigtables.find(child_as);
        if (it == g_child_sigtables.end()) {
            return;
        }
        tbl = it->second;
        g_child_sigtables.erase(it);
    }
    fork_arena::kernel_heap_scope kh;
    delete tbl;
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

void register_pid(pid_t pid, mmu::address_space *as, sched::thread *t)
{
    fork_arena::kernel_heap_scope kh;
    SCOPE_LOCK(g_pid_lock);
    g_as_pid[as] = pid;
    g_pid_as[pid] = as;
    g_pid_thread[pid] = {t, 0};
}

void unregister_pid(pid_t pid, mmu::address_space *as)
{
    fork_arena::kernel_heap_scope kh;
    SCOPE_LOCK(g_pid_lock);
    g_as_pid.erase(as);
    g_pid_as.erase(pid);
    // The caller disposes of the thread next: wait for the signal handlers
    // that hold it to finish interrupting it.
    auto it = g_pid_thread.find(pid);
    while (it != g_pid_thread.end() && it->second.holds) {
        g_pid_thread_cv.wait(&g_pid_lock);
        it = g_pid_thread.find(pid);
    }
    if (it != g_pid_thread.end()) {
        g_pid_thread.erase(it);
    }
}

sched::thread *hold_pid_thread(pid_t pid)
{
    SCOPE_LOCK(g_pid_lock);
    auto it = g_pid_thread.find(pid);
    if (it == g_pid_thread.end()) {
        return nullptr;
    }
    it->second.holds++;
    return it->second.t;
}

void release_pid_thread(pid_t pid)
{
    SCOPE_LOCK(g_pid_lock);
    auto it = g_pid_thread.find(pid);
    assert(it != g_pid_thread.end() && it->second.holds);
    if (--it->second.holds == 0) {
        g_pid_thread_cv.wake_all();
    }
}

// The pid of the "process" the current thread belongs to: a fork child's
// distinct pid, or OSV_PID for the top-level app / kernel (AS0).  getpid()
// routes here so a forked PostgreSQL backend reports a stable, unique pid
// (its MyProcPid) that other backends can target with kill(pid, SIGURG).
pid_t pid_for_current()
{
    auto *as = mmu::current_address_space();
    if (!as || as == mmu::kernel_address_space()) {
        return OSV_PID;
    }
    SCOPE_LOCK(g_pid_lock);
    auto it = g_as_pid.find(as);
    return it != g_as_pid.end() ? it->second : OSV_PID;
}

// The address space of fork child @pid, held (mmu::as_try_hold), or nullptr.
// The map's entry keeps the object alive (see fork()), so the hold is taken
// under the lock.  @registered says whether @pid is a fork child at all: one
// whose address space is retired (it has exited, or exec'd into AS0) is.
mmu::address_space *hold_as_for_pid(pid_t pid, bool *registered)
{
    SCOPE_LOCK(g_pid_lock);
    auto it = g_pid_as.find(pid);
    *registered = it != g_pid_as.end();
    if (*registered && mmu::as_try_hold(it->second)) {
        return it->second;
    }
    return nullptr;
}

// Record the status a child passed to exit(); the first one wins.  The child
// does not become waitable until its resources are released (child_exited).
static void record_exit_status(pid_t child_pid, int status)
{
    SCOPE_LOCK(g_lock);
    auto it = g_children.find(child_pid);
    if (it != g_children.end() && !it->second->status_recorded) {
        it->second->status_recorded = true;
        it->second->status = status;
    }
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
            return;   // already reported; don't re-notify
        }
        it->second->exited = true;
        if (!it->second->status_recorded) {
            it->second->status = status;
        }
        parent = it->second->parent_pid;
        g_cv.wake_all();
    }
    // Notify the parent, Linux-style, that a child changed state.  Address the
    // parent's pid, not getpid(): this may run in the exiting child itself,
    // and a child's signal to its own pid goes to the child.
    kill(parent, SIGCHLD);
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
    // The process's timers die with it.  Do it now rather than at reap time,
    // so a timer cannot fire into an exited process before the reaper runs.
    // An exec'd child runs in AS0, whose timers are the top-level app's.
    auto *as = mmu::current_address_space();
    if (as != mmu::kernel_address_space()) {
        fork_release_process_itimers(as);
    }
    // Encode like Linux wait status for a normal exit: WEXITSTATUS in bits 8-15.
    // The parent is told once the child is torn down.
    record_exit_status(me, (status & 0xff) << 8);
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

// The last use of child address space @as is gone (mmu::as_release): free its
// memory and what is keyed by it only for the running image, then drop the
// reference the retirement holds.  Runs on the thread that dropped the use:
// the reaper or a joiner disposing the last thread, or execve() once it has
// left the child's stack.  The pid and fd state stays until the reap.
extern "C" void fork_retire_address_space(mmu::address_space *as)
{
    // Retired after the reap (a signal handler outlived the child): close the
    // fds that handler opened, which the reap could not see.
    bool reaped;
    WITH_LOCK(g_pid_lock) {
        reaped = g_as_pid.find(as) == g_as_pid.end();
    }
    if (reaped) {
        release_inherited_fds(as);
    }
    fork_release_process_itimers(as);
    fork_release_child_signal_actions(as);
    // The objects this process loaded or holds go with it: any it alone
    // held are unloaded now, without reading their memory (remove_object).
    elf::program::release_as_objects(as);
    mmu::destroy_address_space(as);
    mmu::as_put(as);
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
#if defined(__x86_64__)
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
    void *resume_ctx = &ctx;
    void *caller_ret = reinterpret_cast<void*>(ctx.rip);
    void *caller_sp  = reinterpret_cast<void*>(ctx.rsp);
#elif defined(__aarch64__)
    // aarch64 SAME-VA fork (mirrors x86): capture the caller's full AAPCS64
    // callee-saved register context so the child resumes in fork()'s caller
    // EXACTLY as a normal return would, on the parent's EXACT stack VAs (which
    // clone_address_space privatizes into the child).  fork() is noinline and
    // its prologue saved x19-x28/x29/x30 to its own frame, but those registers
    // STILL hold the caller's values here at the top of fork()'s body (nothing
    // has reloaded them yet), so reading them directly captures the caller's
    // context -- the same trick x86 uses for rbx/r12-r15.
    fork_resume_ctx ctx;
    asm volatile("str x19, %0\n\t str x20, %1\n\t str x21, %2\n\t str x22, %3\n\t"
                 "str x23, %4\n\t str x24, %5\n\t str x25, %6\n\t str x26, %7\n\t"
                 "str x27, %8\n\t str x28, %9\n\t"
                 : "=m"(ctx.x19), "=m"(ctx.x20), "=m"(ctx.x21), "=m"(ctx.x22),
                   "=m"(ctx.x23), "=m"(ctx.x24), "=m"(ctx.x25), "=m"(ctx.x26),
                   "=m"(ctx.x27), "=m"(ctx.x28));
    // fork's frame record: __builtin_frame_address(0) points at fork's saved
    // {x29(caller fp), x30(return addr)} pair.  fp[0] = caller's frame pointer;
    // the caller's SP (where SP was just before `bl fork`) is just above the
    // frame record, mirroring x86's rbp+16.
    void **fp = static_cast<void**>(__builtin_frame_address(0));
    ctx.fp = reinterpret_cast<unsigned long>(fp[0]);          // caller's x29
    ctx.pc = reinterpret_cast<unsigned long>(__builtin_return_address(0));
    ctx.sp = reinterpret_cast<unsigned long>(fp + 2);         // caller's SP
    void *resume_ctx = &ctx;
    void *caller_ret = reinterpret_cast<void*>(ctx.pc);
    void *caller_sp  = reinterpret_cast<void*>(ctx.sp);
#else
#error "fork() caller-context capture not implemented for this architecture"
#endif

    pid_t parent = getpid();

    void *stack_to_free = nullptr;
    // POSIX: run pthread_atfork prepare handlers in the parent before forking.
    __osv_run_atfork_prepare();
    sched::thread *child = fork_thread(caller_ret, caller_sp, resume_ctx, &stack_to_free);
    if (!child) {
        __osv_run_atfork_parent();  // undo prepare-side locking
        errno = ENOMEM;
        return -1;
    }
    pid_t cpid = child->id();
    // The new address space starts with one reference for the fork
    // bookkeeping (the pid and fd maps key by its pointer), dropped by the
    // cleanup below, and its first use is the child thread's.
    mmu::address_space *child_as =
        mmu::clone_address_space(sched::thread::current()->address_space());
    child->set_address_space(child_as);
    // The child maps every object we hold, so it holds them too.
    elf::program::inherit_as_objects(sched::thread::current()->address_space(), child_as);

    // Give the child its OWN reference on every fd it inherited from us (the
    // parent).  OSv shares one global fd table, so without this the child's
    // close() (e.g. PostgreSQL's ClosePostmasterPorts) would tear down a
    // socket/file the parent still holds.  Keyed by the child address space.
    fork::inherit_open_fds(child_as);

    // Give the child its OWN copy of the process signal dispositions, seeded
    // from ours.  Without this the child's sigaction() (e.g. PostgreSQL's
    // startup process resetting SIGCHLD to SIG_DFL) would clobber our shared
    // handler table and the postmaster would never be notified of child exits.
    // A fork child forking again passes on its own table.
    struct sigaction *mine = osv::fork::fork_child_signal_actions();
    osv::fork::fork_init_child_signal_actions(child_as, mine ? mine : osv::signal_actions);
    // Register the child BEFORE starting it so a fast child->exit cannot race
    // ahead of the parent's bookkeeping.
    fork::register_child(cpid, parent);
    // Map the child's pid <-> its address space so getpid() reports this
    // child's distinct pid, and so kill(cpid, SIGURG) can route the latch
    // -wakeup handler into THIS child's address space (where its self-pipe fd
    // resolves).
    fork::register_pid(cpid, child_as, child);

    // The cleanup closure (a std::function, heap-allocated) is stored in the
    // thread object and invoked by the reaper (a kernel thread, AS0); keep it
    // in the identity heap, not the parent's COW arena.
    fork_arena::kernel_heap_scope kh_cleanup;
    // Single cleanup, run by a thread reaper once the (detached) child has
    // fully terminated: free the copied user stack; drop its fds; destroy the
    // child's copy-on-write address space; dispose the child thread object;
    // and only then make the child waitable and send SIGCHLD, so the parent's
    // wait returns once the child's resources are gone, as on Linux.  The
    // status is the one exit() recorded, or 0 if the child ended without it.
    // Disposing is essential: the child thread holds a shared_ptr to its
    // application_runtime (set at thread construction), and only destroying the
    // thread releases it; without it the app runtime never drops to zero,
    // ~application_runtime never fires, application::join() blocks forever on
    // _terminated, and OSv hangs at shutdown instead of powering off.
    child->set_cleanup([cpid, stack_to_free, child, child_as] {
        // dispose() below destroys this closure; keep what follows it local.
        pid_t pid = cpid;
        mmu::address_space *as = child_as;
        fork::unregister_pid(cpid, child_as);
        if (stack_to_free) {
            free(stack_to_free);
        }
        // Drop any inherited-fd references the child still held (its close()s
        // dropped the ones it explicitly closed; this releases the rest, so
        // the parent's fd refcounts return to just the parent's own).
        fork::release_inherited_fds(child_as);
        // Disposing the thread drops its use of the child address space; if
        // it was the last (no signal handler still runs there, and the child
        // did not exec), the address space is retired here.
        sched::thread::dispose(child);
        // The fork bookkeeping's reference: the pid and fd maps are done with
        // the pointer, so a later address space may reuse it.
        mmu::as_put(as);
        fork::child_exited(pid, 0);
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
    // Map to fork(). A correct vfork() caller only calls execve() or _exit()
    // in the child, so it cannot tell a copy-on-write child from one that
    // borrows the parent's memory.
    // fork() is ::fork() here (an unqualified `fork` would resolve to the
    // osv::fork namespace brought in by `using namespace osv`).
    return ::fork();
}
