/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// inotify(2): watch filesystem paths for changes and read events from an fd.
//
// OSv's VFS routes all path mutations through a handful of central functions in
// fs/vfs/vfs_syscalls.cc.  Those call osv_inotify_notify() below with the
// absolute path affected and the event mask.  This module keeps a registry of
// inotify instances, matches each event against their watches, and queues a
// struct inotify_event for the fd (waking any poller).
//
// A watch on a directory reports IN_CREATE (from open(O_CREAT), mkdir(),
// mknod(), link() and symlink()), IN_DELETE (unlink(), rmdir()) and
// IN_MOVED_FROM/IN_MOVED_TO (rename(), the pair sharing a non-zero cookie) for
// the entries in it, with the entry name.  A watch on the object itself reports
// IN_DELETE_SELF followed by IN_IGNORED (and the watch is removed) when it is
// deleted, and IN_MOVE_SELF when it is renamed.
//
// Not supported (a watch that asks for any of these gets a one-time warning on
// the console, not an error, because programs usually ask for a mix):
// - These events are never generated: IN_ACCESS, IN_MODIFY, IN_ATTRIB,
//   IN_CLOSE_WRITE, IN_CLOSE_NOWRITE, IN_OPEN, IN_UNMOUNT and IN_Q_OVERFLOW.
// - These flags are accepted and ignored: IN_DONT_FOLLOW and IN_EXCL_UNLINK.
// - Watches follow paths, not inodes.  A watched directory that is renamed
//   reports IN_MOVE_SELF and then goes silent (Linux keeps reporting events
//   from its new location), and the watch reports events for whatever is
//   later created at the old path.  For the same reason a watch on a hard
//   link sees only changes made through that path.
// - The event queue has no size limit, where Linux caps it at
//   max_queued_events and then reports IN_Q_OVERFLOW.

#include <sys/inotify.h>
#include <sys/stat.h>
#include <fs/fs.hh>
#include <libc/libc.hh>
#include <osv/fcntl.h>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/poll.h>
#include <osv/stubbing.hh>
#include <fs/vfs/vfs.h>

#include <atomic>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstring>
#include <climits>
#include <unistd.h>
#include <errno.h>

namespace {

class inotify_fd;

mutex registry_lock;
std::set<inotify_fd*> registry;
// The number of live instances, so the VFS hooks skip registry_lock when no
// program uses inotify.  A hook that races the creation of the first instance
// may miss an event for a mutation that the new instance had no watch for yet.
std::atomic<int> live_instances{0};

// Requested events this implementation never generates, and flags it ignores.
constexpr uint32_t unsupported_mask = IN_ACCESS | IN_MODIFY | IN_ATTRIB |
    IN_CLOSE_WRITE | IN_CLOSE_NOWRITE | IN_OPEN | IN_DONT_FOLLOW | IN_EXCL_UNLINK;

struct watch {
    int         wd;
    std::string path;   // absolute, no trailing slash (except root "/")
    uint32_t    mask;
};

// Split an absolute path into (parent-dir, basename).  "/a/b" -> ("/a", "b").
static void split_path(const std::string& path, std::string& dir, std::string& name)
{
    auto slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        dir = ".";
        name = path;
    } else if (slash == 0) {
        dir = "/";
        name = path.substr(1);
    } else {
        dir = path.substr(0, slash);
        name = path.substr(slash + 1);
    }
}

class inotify_fd final : public special_file {
public:
    explicit inotify_fd(int flags)
        : special_file(FREAD | flags, DTYPE_UNSPEC)
    {
        WITH_LOCK(registry_lock) { registry.insert(this); }
        live_instances++;
    }

    int close() override {
        WITH_LOCK(registry_lock) { registry.erase(this); }
        live_instances--;
        return 0;
    }

    int read(struct uio* uio, int flags) override;
    int poll(int events) override;

    int add_watch(const std::string& path, uint32_t mask);
    int rm_watch(int wd);

    // Match an event against this instance's watches and queue it if it fits.
    // Returns true if an event was queued.
    bool notify(const std::string& dir, const std::string& name,
                const std::string& full, uint32_t mask, uint32_t cookie);

private:
    void queue_event(int wd, uint32_t mask, uint32_t cookie, const std::string& name);

    mutable mutex _mutex;
    condvar       _blocked_reader;
    int           _next_wd = 1;
    bool          _warned = false;   // the unsupported-events warning was shown
    std::vector<watch> _watches;
    std::deque<std::vector<char>> _events;   // each is a packed inotify_event
};

void inotify_fd::queue_event(int wd, uint32_t mask, uint32_t cookie, const std::string& name)
{
    // Pack: struct inotify_event header + NUL-terminated name padded to a
    // multiple of the struct's alignment (Linux rounds the name field up).
    size_t namelen = name.empty() ? 0 : ((name.size() + 1 + 15) & ~size_t(15));
    std::vector<char> buf(sizeof(inotify_event) + namelen, 0);
    auto* ev = reinterpret_cast<inotify_event*>(buf.data());
    ev->wd = wd;
    ev->mask = mask;
    ev->cookie = cookie;
    ev->len = namelen;
    if (!name.empty()) {
        memcpy(buf.data() + sizeof(inotify_event), name.c_str(), name.size());
    }
    _events.push_back(std::move(buf));
    _blocked_reader.wake_all();
}

// Match an event against this instance's watches; queue it for each matching
// watch.  Returns true if at least one event was queued (so the caller only
// wakes pollers when there is something to read).
bool inotify_fd::notify(const std::string& dir, const std::string& name,
                        const std::string& full, uint32_t mask, uint32_t cookie)
{
    bool queued = false;
    WITH_LOCK(_mutex) {
        for (auto it = _watches.begin(); it != _watches.end(); ) {
            auto& w = *it;
            uint32_t ev = 0;
            uint32_t ev_cookie = 0;
            std::string ev_name;
            bool gone = false;
            if (w.path == dir) {
                // A directory watch: report the affected entry name.
                ev = mask;
                ev_cookie = cookie;
                ev_name = name;
            } else if (w.path == full) {
                // A watch on the object itself gets no directory-entry
                // events, only IN_DELETE_SELF and IN_MOVE_SELF, with no name.
                // IN_MOVED_TO here means a rename replaced the object.  Linux
                // follows a deleted object with IN_IGNORED and drops the
                // watch, whatever the watch asked for.
                if (mask & (IN_DELETE | IN_MOVED_TO)) {
                    ev = IN_DELETE_SELF;
                    gone = true;
                } else if (mask & IN_MOVED_FROM) {
                    ev = IN_MOVE_SELF;
                }
            }
            if (w.mask & ev & IN_ALL_EVENTS) {
                queue_event(w.wd, ev, ev_cookie, ev_name);
                queued = true;
                gone |= (w.mask & IN_ONESHOT) != 0;
            }
            if (gone) {
                queue_event(w.wd, IN_IGNORED, 0, std::string());
                queued = true;
                it = _watches.erase(it);
            } else {
                ++it;
            }
        }
    }
    // Only wake pollers when an event was actually enqueued for this instance;
    // otherwise a non-matching mutation would cause spurious POLLIN wakeups.
    if (queued) {
        poll_wake(this, POLLIN);
    }
    return queued;
}

int inotify_fd::add_watch(const std::string& path, uint32_t mask)
{
    WITH_LOCK(_mutex) {
        if ((mask & unsupported_mask) && !_warned) {
            _warned = true;
            debug_always("WARNING: inotify_add_watch(%s, 0x%x): OSv does not "
                "support the events or flags 0x%x; they will never be reported\n",
                path.c_str(), mask, mask & unsupported_mask);
        }
        // Existing watch on the same path: merge the mask (or replace unless
        // IN_MASK_ADD), and keep its wd, like Linux.  IN_MASK_CREATE asks
        // for a new watch only.
        for (auto& w : _watches) {
            if (w.path == path) {
                if (mask & IN_MASK_CREATE) {
                    return -EEXIST;
                }
                w.mask = (mask & IN_MASK_ADD) ? (w.mask | mask) : mask;
                return w.wd;
            }
        }
        int wd = _next_wd++;
        _watches.push_back(watch{wd, path, mask});
        return wd;
    }
}

int inotify_fd::rm_watch(int wd)
{
    WITH_LOCK(_mutex) {
        for (auto it = _watches.begin(); it != _watches.end(); ++it) {
            if (it->wd == wd) {
                _watches.erase(it);
                // Linux queues an IN_IGNORED for the removed watch.
                queue_event(wd, IN_IGNORED, 0, std::string());
                _blocked_reader.wake_all();
                return 0;
            }
        }
    }
    errno = EINVAL;
    return -1;
}

int inotify_fd::read(struct uio* uio, int flags)
{
    WITH_LOCK(_mutex) {
        while (_events.empty()) {
            if (f_flags & FNONBLOCK) {
                return EAGAIN;
            }
            _blocked_reader.wait(_mutex);
        }
        // The first event must fit; a too-small buffer is EINVAL (Linux).
        if (uio->uio_resid < (ssize_t)_events.front().size()) {
            return EINVAL;
        }
        while (!_events.empty() &&
               uio->uio_resid >= (ssize_t)_events.front().size()) {
            auto& ev = _events.front();
            int err = uiomove(ev.data(), ev.size(), uio);
            if (err) {
                return err;
            }
            _events.pop_front();
        }
    }
    return 0;
}

int inotify_fd::poll(int events)
{
    int rc = 0;
    WITH_LOCK(_mutex) {
        if (!_events.empty() && (events & POLLIN)) {
            rc |= POLLIN;
        }
    }
    return rc;
}

} // anonymous namespace

// VFS hook: called from fs/vfs/vfs_syscalls.cc after a successful mutation.
// `path` is the absolute path affected; `mask` is the IN_* event; `is_dir`
// marks the affected object as a directory (adds IN_ISDIR); `cookie` correlates
// an IN_MOVED_FROM/IN_MOVED_TO pair (0 for non-move events).
extern "C" void osv_inotify_notify(const char* path, uint32_t mask, int is_dir,
                                   uint32_t cookie)
{
    if (!path) {
        return;
    }
    if (live_instances.load(std::memory_order_relaxed) == 0) {
        return;
    }
    if (is_dir) {
        mask |= IN_ISDIR;
    }
    // path_conv() keeps a trailing slash (mkdir("d/sub/")); watches have none.
    std::string full(path);
    while (full.size() > 1 && full.back() == '/') {
        full.pop_back();
    }
    std::string dir, name;
    split_path(full, dir, name);

    WITH_LOCK(registry_lock) {
        for (auto* in : registry) {
            in->notify(dir, name, full, mask, cookie);
        }
    }
}

extern "C" OSV_LIBC_API
int inotify_init1(int flags)
{
    if (flags & ~(IN_CLOEXEC | IN_NONBLOCK)) {
        return libc_error(EINVAL);
    }
    int of = 0;
    if (flags & IN_NONBLOCK) {
        of |= O_NONBLOCK;
    }
    if (flags & IN_CLOEXEC) {
        of |= O_CLOEXEC;
    }
    try {
        fileref f = make_file<inotify_fd>(of);
        fdesc fd(f);
        return fd.release();
    } catch (int error) {
        return libc_error(error);
    }
}

extern "C" OSV_LIBC_API
int inotify_init()
{
    return inotify_init1(0);
}

extern "C" OSV_LIBC_API
int inotify_add_watch(int fd, const char* pathname, uint32_t mask)
{
    if (!pathname) {
        return libc_error(EINVAL);
    }
    // Linux rejects an empty mask, and IN_MASK_ADD with IN_MASK_CREATE.
    if (mask == 0 ||
        (mask & (IN_MASK_ADD | IN_MASK_CREATE)) == (IN_MASK_ADD | IN_MASK_CREATE)) {
        return libc_error(EINVAL);
    }
    fileref f(fileref_from_fd(fd));
    if (!f) {
        return libc_error(EBADF);
    }
    auto* in = dynamic_cast<inotify_fd*>(f.get());
    if (!in) {
        return libc_error(EINVAL);
    }
    if (!pathname[0]) {
        return libc_error(ENOENT);
    }
    // Resolve the path the way the VFS does (path_conv() drops "." and ".."
    // components), so it matches the paths the VFS hooks report.
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        return libc_error(errno);
    }
    char abs[PATH_MAX];
    int error = path_conv(cwd, pathname, abs);
    if (error) {
        return libc_error(error);
    }
    // Strip a trailing slash (except for root).
    size_t n = strlen(abs);
    while (n > 1 && abs[n - 1] == '/') {
        abs[--n] = '\0';
    }
    // Linux fails on a nonexistent path (ENOENT), and on a non-directory when
    // IN_ONLYDIR is set (ENOTDIR).  Check before registering so callers do not
    // silently watch something that is not there.
    struct stat st;
    if (stat(abs, &st) != 0) {
        return libc_error(errno);
    }
    if ((mask & IN_ONLYDIR) && !S_ISDIR(st.st_mode)) {
        return libc_error(ENOTDIR);
    }
    int wd = in->add_watch(abs, mask);
    if (wd < 0) {
        return libc_error(-wd);
    }
    return wd;
}

extern "C" OSV_LIBC_API
int inotify_rm_watch(int fd, int wd)
{
    fileref f(fileref_from_fd(fd));
    if (!f) {
        return libc_error(EBADF);
    }
    auto* in = dynamic_cast<inotify_fd*>(f.get());
    if (!in) {
        return libc_error(EINVAL);
    }
    return in->rm_watch(wd);
}
