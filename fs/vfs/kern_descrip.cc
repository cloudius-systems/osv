/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <osv/file.h>
#include <osv/poll.h>
#include <osv/debug.h>
#include <osv/mutex.h>
#include <osv/rcu.hh>
#include <osv/export.h>
#include <boost/range/algorithm/find.hpp>

#include <bsd/sys/sys/queue.h>
#include <osv/kernel_config_fork.h>
#if CONF_fork
#include <osv/fork_arena.hh>
#endif

#include <osv/kernel_config_lazy_stack.h>
#include <osv/kernel_config_lazy_stack_invariant.h>
#include <osv/kernel_config_core_epoll.h>

using namespace osv;

#if CONF_fork
// fork() fd-inheritance hooks (libc/process/fork.cc): record a child's own
// post-fork fd allocation, and query whether a child owns @fd.  Declared here
// (before _fdalloc/fdclose) so both can call them.
extern "C" void fork_child_opened_fd(int fd);
extern "C" bool fork_child_owns_fd(int fd);
#endif

/*
 * Global file descriptors table - in OSv we have a single process so file
 * descriptors are maintained globally.
 */
rcu_ptr<file> gfdt[FDMAX] = {};
mutex_t gfdt_lock = MUTEX_INITIALIZER;

/*
 * Allocate a file descriptor and assign fd to it atomically.
 *
 * Grabs a reference on fp if successful.
 */
int _fdalloc(struct file *fp, int *newfd, int min_fd)
{
    int fd;

    fhold(fp);

    for (fd = min_fd; fd < FDMAX; fd++) {
        if (gfdt[fd])
            continue;

        WITH_LOCK(gfdt_lock) {
            /* Now that we hold the lock,
             * make sure the entry is still available */
            if (gfdt[fd].read_by_owner()) {
                continue;
            }

            /* Install */
            gfdt[fd].assign(fp);
            *newfd = fd;
        }

#if CONF_fork
        // If a fork child allocated this fd, record it so the child's later
        // close() of it nulls the shared slot normally (its own fd), as opposed
        // to a numeric handle onto one of the parent's fds (which the child
        // must not null on OSv's single shared fd table).
        fork_child_opened_fd(fd);
#endif
        return 0;
    }

    fdrop(fp);
    return EMFILE;
}

extern "C" OSV_LIBC_API
int getdtablesize(void)
{
    return FDMAX;
}

/*
 * Allocate a file descriptor and assign fd to it atomically.
 *
 * Grabs a reference on fp if successful.
 */
int fdalloc(struct file *fp, int *newfd)
{
    return (_fdalloc(fp, newfd, 0));
}

#if CONF_fork
// fork() fd-inheritance hook (libc/process/fork.cc).  OSv keeps a SINGLE global
// fd table shared by a fork parent and child, so a child's close() would
// otherwise clear the shared gfdt slot and drop the only file reference --
// tearing down a socket/file the parent still holds.  For an fd the current
// fork child INHERITED from its parent, this drops only the child's inherited
// reference and leaves the shared slot intact; it returns true when it handled
// the close so fdclose() must not touch gfdt[].  Returns false (normal close)
// for the top-level app and for fds the child opened itself after fork.
extern "C" bool fork_child_close_inherited_fd(int fd);
// A non-child (top-level app / postmaster) closing an fd that a LIVE fork child
// inherited: drop the owner's reference but leave the shared gfdt slot intact
// for the child (which finds the file via gfdt[fd] on OSv's single fd table).
// Returns true when it handled the close (slot kept), false for a normal close.
extern "C" bool fork_owner_close_inherited_fd(int fd, struct file *fp);
#endif

int fdclose(int fd)
{
    struct file* fp;

#if CONF_fork
    // A fork child closing an fd it inherited only drops its own reference; the
    // shared gfdt slot and the underlying file stay alive for the parent (or,
    // if the parent already closed it, until the last inheriting child goes).
    if (fork_child_close_inherited_fd(fd)) {
        return 0;
    }
#endif

#if CONF_fork
    bool owner_keep_slot = false;
    // A fork child closing an fd it did NOT inherit and did NOT open itself is
    // holding only a numeric handle onto one of the PARENT's fds (OSv has one
    // shared global fd table).  Nulling gfdt[fd] here would tear that fd out
    // from under the parent -- exactly what corrupted PostgreSQL's postmaster
    // when a backend's ClosePostmasterPorts() closed the postmaster-owned
    // death-watch pipe fd.  Treat it as a harmless no-op on the shared slot
    // (POSIX: the fd is "closed" in the child's process view).
    if (!fork_child_owns_fd(fd)) {
        return 0;
    }
#endif

    WITH_LOCK(gfdt_lock) {

        fp = gfdt[fd].read_by_owner();
        if (fp == nullptr) {
            return EBADF;
        }

#if CONF_fork
        // The top-level owner (postmaster) closing a connection fd that a live
        // forked child still inherits: keep the shared gfdt slot pointing at
        // the socket so the child's getsockname()/recv() on the inherited fd
        // still resolves it (nulling the slot -> child sees EBADF, or a reused
        // vnode -> ENOTSOCK).  Leave the slot; only the owner's reference is
        // dropped (below, outside gfdt_lock).
        owner_keep_slot = fork_owner_close_inherited_fd(fd, fp);
        if (!owner_keep_slot) {
            gfdt[fd].assign(nullptr);
        }
#else
        gfdt[fd].assign(nullptr);
#endif
    }

    fdrop(fp);

    return 0;
}

#if CONF_fork
// Null gfdt[fd] iff it currently holds @fp.  Used by fork's inherited-fd
// bookkeeping (libc/process/fork.cc) to release a shared fd slot once its last
// live inheriting child is done with it, without disturbing a slot that has
// since been reassigned to a different file.
extern "C" void fork_clear_gfdt_slot_if(int fd, struct file *fp)
{
    if (fd < 0 || fd >= FDMAX) {
        return;
    }
    WITH_LOCK(gfdt_lock) {
        if (gfdt[fd].read_by_owner() == fp) {
            gfdt[fd].assign(nullptr);
        }
    }
}
#endif

#if CONF_fork
// Snapshot the currently-open fds and take one reference (fhold) on each open
// file, storing the fd->file pairs via the supplied callback.  Used by fork()
// to give the child its OWN reference on every inherited open file, so the
// child's later close()/exit drops only the child's ref rather than tearing
// down a file the parent still uses.  Runs under gfdt_lock so the snapshot is
// consistent against concurrent fdalloc/fdclose.
extern "C" void fork_snapshot_open_fds(void (*emit)(void *ctx, int fd, struct file *fp),
                                       void *ctx)
{
    WITH_LOCK(gfdt_lock) {
        for (int fd = 0; fd < FDMAX; fd++) {
            struct file *fp = gfdt[fd].read_by_owner();
            if (fp) {
                fhold(fp);
                emit(ctx, fd, fp);
            }
        }
    }
}
#endif

/*
 * Assigns a file pointer to a specific file descriptor.
 * Grabs a reference to the file pointer if successful.
 */
int fdset(int fd, struct file *fp)
{
    struct file *orig;

    if (fd < 0 || fd >= FDMAX)
        return EBADF;

    fhold(fp);

    WITH_LOCK(gfdt_lock) {
        orig = gfdt[fd].read_by_owner();
        /* Install new file structure in place */
        gfdt[fd].assign(fp);
    }

    if (orig)
        fdrop(orig);

    return 0;
}

static bool fhold_if_positive(file* f)
{
    auto c = f->f_count;
    // zero or negative f_count means that the file is being closed; don't
    // increment
    while (c > 0 && !__atomic_compare_exchange_n(&f->f_count, &c, c + 1, true,
            __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        // nothing to do
    }
    return c > 0;
}

/*
 * Retrieves a file structure from the gfdt and increases its refcount in a
 * synchronized way, this ensures that a concurrent close will not interfere.
 */
int fget(int fd, struct file **out_fp)
{
    struct file *fp;

    if (fd < 0 || fd >= FDMAX)
        return EBADF;

#if CONF_lazy_stack_invariant
    assert(sched::preemptable() && arch::irq_enabled());
#endif
#if CONF_lazy_stack
    arch::ensure_next_stack_page();
#endif
    WITH_LOCK(rcu_read_lock) {
        fp = gfdt[fd].read();
        if (fp == nullptr) {
            return EBADF;
        }

        if (!fhold_if_positive(fp)) {
            return EBADF;
        }
    }

    *out_fp = fp;
    return 0;
}

file::file(unsigned flags, filetype_t type, void *opaque)
    : f_flags(flags)
    , f_count(1)
    , f_data(opaque)
    , f_type(type)
{
}

void file::wake_epoll(int events)
{
#if CONF_core_epoll
    WITH_LOCK(f_lock) {
        if (!f_epolls) {
            return;
        }
        for (auto&& ep : *f_epolls) {
            epoll_wake(ep);
        }
    }
#endif
}

void fhold(struct file* fp)
{
    __sync_fetch_and_add(&fp->f_count, 1);
}

OSV_LIBSOLARIS_API
int fdrop(struct file *fp)
{
    int o = fp->f_count, n;
    bool do_free;
    do {
        n = o - 1;
        if (n == 0) {
            /* We are about to free this file structure, but we still do things with it
             * so set the refcount to INT_MIN, fhold/fdrop may get called again
             * and we don't want to reach this point more than once.
             * INT_MIN is also safe against fget() seeing this file.
             */
            n = INT_MIN;
            do_free = true;
        } else {
            do_free = false;
        }
    } while (!__atomic_compare_exchange_n(&fp->f_count, &o, n, true,
                __ATOMIC_RELAXED, __ATOMIC_RELAXED));

    if (!do_free)
        return 0;

    fp->stop_polls();
    fp->close();
    rcu_dispose(fp);
    return 1;
}

file::~file()
{
}

void file::stop_polls()
{
    auto fp = this;

    poll_drain(fp);
#if CONF_core_epoll
    if (f_epolls) {
        for (auto ep : *f_epolls) {
            epoll_file_closed(ep);
        }
    }
#endif
}

void file::epoll_add(epoll_ptr ep)
{
#if CONF_core_epoll
    WITH_LOCK(f_lock) {
#if CONF_fork
        // f_epolls records which epoll instances are watching this file.  The
        // struct file is SHARED across forked processes (identity heap), and
        // the WAKE side runs cross-AS: when data arrives, the RX/netisr thread
        // (or another backend) walks so->fp->f_epolls and calls epoll_wake for
        // each watcher (see tcp_input.cc, tcp_usrreq.cc).  If the vector (and
        // its element storage) lived in the registering backend's COW fork
        // arena, the waking thread's address space would see a divergent/empty
        // f_epolls -> it never wakes the epoll -> the watching PG backend hangs
        // in epoll_wait forever (concurrent connections stall).  Keep the
        // vector and its growth on the identity kernel heap so every address
        // space sees the same watcher list -- same rule as the struct file it
        // hangs off.
        fork_arena::kernel_heap_scope _fkh;
#endif
        if (!f_epolls) {
            f_epolls.reset(new std::vector<epoll_ptr>);
        }
        if (boost::range::find(*f_epolls, ep) == f_epolls->end()) {
            f_epolls->push_back(ep);
        }
    }
#endif
}

void file::epoll_del(epoll_ptr ep)
{
#if CONF_core_epoll
    WITH_LOCK(f_lock) {
        assert(f_epolls);
        auto i = boost::range::find(*f_epolls, ep);
        if (i != f_epolls->end()) {
            f_epolls->erase(i);
        }
    }
#endif
}

OSV_LIBSOLARIS_API
dentry* file_dentry(file* fp)
{
    return fp->f_dentry.get();
}

void file_setdata(file* fp, void* data)
{
    fp->f_data = data;
}

bool is_nonblock(struct file *f)
{
    return (f->f_flags & FNONBLOCK);
}

OSV_LIBSOLARIS_API
int file_flags(file *f)
{
    return f->f_flags;
}

OSV_LIBSOLARIS_API
off_t file_offset(file* f)
{
    return f->f_offset;
}

OSV_LIBSOLARIS_API
void file_setoffset(file* f, off_t o)
{
    f->f_offset = o;
}

void* file_data(file* f)
{
    return f->f_data;
}

filetype_t file_type(file* f)
{
    return f->f_type;
}
