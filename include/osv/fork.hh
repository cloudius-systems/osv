/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_FORK_HH
#define OSV_FORK_HH

#include <sys/types.h>
#include <osv/app.hh>

// fork() emulation on OSv.
//
// OSv is a single-address-space unikernel with no MMU-enforced process
// isolation, so a true copy-on-write fork() is impossible.  We implement the
// useful, compatible subset (see documentation/fork.md):
//
//  * fork() runs the child as an OSv thread that shares the parent's address
//    space (heap, globals, fds) but gets a PRIVATE COPY of the parent's user
//    stack, so both parent and child return from fork() with their own local
//    variables and return address (the classic "fork returns twice").
//  * fork()+execve() works: the child execve()s a new program (which OSv runs
//    as a fresh application/ELF-namespace) and the parent waitpid()s for it.
//  * waitpid()/wait4() reap the child's exit status; SIGCHLD is raised to the
//    parent on child exit.
//
// It CANNOT provide memory isolation: a child that mutates shared globals/heap
// expecting a private copy will affect the parent.  fork()-as-memory-snapshot
// (e.g. Redis BGSAVE) is unsupported.

namespace osv {
namespace fork {

// Called by execve() to record the application it launched, so that when this
// (child) thread later exits, waitpid() in the parent can report the exec'd
// program's exit status under the child's pid.
void adopt_execed_app(shared_app_t app);

// Register the current thread as a fork() child of @parent_pid with child pid
// @child_pid.  Called on the child just before it resumes at the fork() return
// site.  Sets up the child's exit hook so the parent's waitpid() can reap it.
void register_child(pid_t child_pid, pid_t parent_pid);

// Record that child @child_pid exited with @status (encoded WIFEXITED-style),
// wake any waiter, and raise SIGCHLD to the parent.  Called from the child's
// exit path.
void child_exited(pid_t child_pid, int status);

// waitpid(2)/wait4(2) backend: block (unless WNOHANG) for a child of the
// current thread to exit, reap it, and return its pid.  Returns -1/ECHILD if
// the caller has no matching children.
pid_t wait_child(pid_t pid, int *status, int options);

// If the current thread is a fork() child (or an exec'd child app), record its
// exit @status for the parent's waitpid(), notify the parent (SIGCHLD), and
// return true so exit() ends only this thread instead of shutting down OSv.
// Returns false for the top-level application (exit() shuts down as before).
bool exit_current_child(int status);

} // namespace fork
} // namespace osv

// The syscall/libc entry points.
extern "C" pid_t fork(void);
extern "C" pid_t vfork(void);

#endif // OSV_FORK_HH
