/*
 * Copyright (C) 2026 Waldemar Kozaczuk
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
// A watch on a directory fires for changes to entries within it (with the entry
// name); a watch on a file fires for changes to the file itself.  This covers
// the common events (IN_CREATE, IN_DELETE, IN_MODIFY, IN_MOVED_FROM/TO,
// IN_CLOSE_WRITE, IN_ATTRIB); the recursive-watch and mount-related events are
// out of scope.

#include <sys/inotify.h>
#include <fs/fs.hh>
#include <libc/libc.hh>
#include <osv/fcntl.h>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/poll.h>

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstring>
#include <errno.h>

namespace {

class inotify_fd;

mutex registry_lock;
std::set<inotify_fd*> registry;

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
    }

    int close() override {
        WITH_LOCK(registry_lock) { registry.erase(this); }
        return 0;
    }

    int read(struct uio* uio, int flags) override;
    int poll(int events) override;

    int add_watch(const std::string& path, uint32_t mask);
    int rm_watch(int wd);

    // Match an event against this instance's watches and queue it if it fits.
    void notify(const std::string& dir, const std::string& name,
                const std::string& full, uint32_t mask);

private:
    void queue_event(int wd, uint32_t mask, const std::string& name);

    mutable mutex _mutex;
    condvar       _blocked_reader;
    int           _next_wd = 1;
    std::vector<watch> _watches;
    std::deque<std::vector<char>> _events;   // each is a packed inotify_event
};

void inotify_fd::queue_event(int wd, uint32_t mask, const std::string& name)
{
    // Pack: struct inotify_event header + NUL-terminated name padded to a
    // multiple of the struct's alignment (Linux rounds the name field up).
    size_t namelen = name.empty() ? 0 : ((name.size() + 1 + 15) & ~size_t(15));
    std::vector<char> buf(sizeof(inotify_event) + namelen, 0);
    auto* ev = reinterpret_cast<inotify_event*>(buf.data());
    ev->wd = wd;
    ev->mask = mask;
    ev->cookie = 0;
    ev->len = namelen;
    if (!name.empty()) {
        memcpy(buf.data() + sizeof(inotify_event), name.c_str(), name.size());
    }
    _events.push_back(std::move(buf));
    _blocked_reader.wake_all();
}

void inotify_fd::notify(const std::string& dir, const std::string& name,
                        const std::string& full, uint32_t mask)
{
    WITH_LOCK(_mutex) {
        bool queued = false;
        for (auto& w : _watches) {
            if (!(w.mask & (mask & IN_ALL_EVENTS))) {
                continue;
            }
            if (w.path == dir) {
                // A directory watch: report the affected entry name.
                queue_event(w.wd, mask, name);
                queued = true;
            } else if (w.path == full) {
                // A watch on the file/dir itself: no name.
                queue_event(w.wd, mask, std::string());
                queued = true;
            }
        }
        (void)queued;
    }
    poll_wake(this, POLLIN);
}

int inotify_fd::add_watch(const std::string& path, uint32_t mask)
{
    WITH_LOCK(_mutex) {
        // Existing watch on the same path: merge the mask (or replace unless
        // IN_MASK_ADD), and keep its wd, like Linux.
        for (auto& w : _watches) {
            if (w.path == path) {
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
                queue_event(wd, IN_IGNORED, std::string());
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
// marks the affected object as a directory (adds IN_ISDIR).
extern "C" void osv_inotify_notify(const char* path, uint32_t mask, int is_dir)
{
    if (!path) {
        return;
    }
    // Fast path: nothing is watching.
    WITH_LOCK(registry_lock) {
        if (registry.empty()) {
            return;
        }
    }
    if (is_dir) {
        mask |= IN_ISDIR;
    }
    std::string full(path);
    std::string dir, name;
    split_path(full, dir, name);

    WITH_LOCK(registry_lock) {
        for (auto* in : registry) {
            in->notify(dir, name, full, mask);
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
    fileref f(fileref_from_fd(fd));
    if (!f) {
        return libc_error(EBADF);
    }
    auto* in = dynamic_cast<inotify_fd*>(f.get());
    if (!in) {
        return libc_error(EINVAL);
    }
    // Resolve to an absolute path so it matches what the VFS hook reports.
    char abs[PATH_MAX];
    if (pathname[0] == '/') {
        strlcpy(abs, pathname, sizeof(abs));
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) {
            return libc_error(errno);
        }
        snprintf(abs, sizeof(abs), "%s/%s", cwd, pathname);
    }
    // Strip a trailing slash (except for root).
    size_t n = strlen(abs);
    while (n > 1 && abs[n - 1] == '/') {
        abs[--n] = '\0';
    }
    return in->add_watch(abs, mask);
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
