/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Implement the Linux epoll(7) functions in OSV

// NOTE: This is an inefficient implementation, in which epoll_wait() calls
// poll(), thereby negating all the performance benefits of using epoll and
// not poll. This is only a temporary implementation, for getting the
// functionality of epoll which Java needs - but not its performance.

#include <sys/epoll.h>
#include <sys/poll.h>
#include <memory>
#include <errno.h>


#include <osv/file.h>
#include <osv/poll.h>
#include <fs/fs.hh>
#include <fs/unsupported.h>
#include <drivers/clock.hh>

#include <debug.hh>
#include <unordered_map>
#include <boost/range/algorithm/find.hpp>

#include <osv/trace.hh>
TRACEPOINT(trace_epoll_create, "returned fd=%d", int);
TRACEPOINT(trace_epoll_ctl, "epfd=%d, fd=%d, op=%s", int, int, const char*);
TRACEPOINT(trace_epoll_wait, "epfd=%d, maxevents=%d, timeout=%d", int, int, int);
TRACEPOINT(trace_epoll_ready, "file=%p, event=0x%x", file*, int);

// We implement epoll using poll(), and therefore need to convert epoll's
// event bits to and poll(). These are mostly the same, so the conversion
// is trivial, but we verify this here with static_asserts. We also don't
// support epoll-only flags (namely EPOLLET and EPOLLONESHOT) and assert
// this at runtime.
static_assert(POLLIN == EPOLLIN, "POLLIN!=EPOLLIN");
static_assert(POLLOUT == EPOLLOUT, "POLLOUT!=EPOLLOUT");
static_assert(POLLRDHUP == EPOLLRDHUP, "POLLRDHUP!=EPOLLRDHUP");
static_assert(POLLPRI == EPOLLPRI, "POLLPRI!=EPOLLPRI");
static_assert(POLLERR == EPOLLERR, "POLLERR!=EPOLLERR");
static_assert(POLLHUP == EPOLLHUP, "POLLHUP!=EPOLLHUP");
constexpr int SUPPORTED_EVENTS =
        EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLPRI | EPOLLERR | EPOLLHUP;
inline uint32_t events_epoll_to_poll(uint32_t e)
{
    static bool warned;
    if ((e & EPOLLET) & !warned) {
        warned = true;
        debug("EPOLLET ignored\n");
    }
    e &= ~EPOLLET;
    assert (!(e & ~SUPPORTED_EVENTS));
    return e;
}
inline uint32_t events_poll_to_epoll(uint32_t e)
{
    assert (!(e & ~SUPPORTED_EVENTS));
    return e;
}

class epoll_obj {
    std::unordered_map<file*, epoll_event> map;
    file* epoll_fp;
public:
    void set_epoll_fp(file* fp) { epoll_fp = fp; }
    ~epoll_obj() {
        for (auto& e : map) {
            auto fp = e.first;
            remove_me(fp);
        }
    }
    int add(file* fp, struct epoll_event *event)
    {
        if (map.count(fp)) {
            return EEXIST;
        }
        map.emplace(std::move(fp), *event);
        WITH_LOCK(fp->f_lock) {
            if (!fp->f_epolls) {
                fp->f_epolls.reset(new std::vector<file*>);
            }
            fp->f_epolls->push_back(epoll_fp);
        }
        return 0;
    }
    int mod(file* fp, struct epoll_event *event)
    {
        try {
            map.at(fp) = *event;
            return 0;
        } catch (std::out_of_range &e) {
            return ENOENT;
        }
    }
    int del(file* fp)
    {
        if (map.erase(fp)) {
            remove_me(fp);
            return 0;
        } else {
            return ENOENT;
        }
    }
    int wait(struct epoll_event *events, int maxevents, int timeout_ms)
    {
        std::vector<poll_file> pollfds;
        pollfds.reserve(map.size());
        for (auto &i : map) {
            int eevents = i.second.events;
            auto events = events_epoll_to_poll(eevents);
            pollfds.emplace_back(i.first, events, 0);
        }
        int r = do_poll(pollfds, timeout_ms);
        if (r > 0) {
            r = std::min(r, maxevents);
            int remain = r;
            for (size_t i = 0; i < pollfds.size() && remain;  i++) {
                if (pollfds[i].revents) {
                    --remain;
                    assert(pollfds[i].fp);
                    events[remain].data = map[pollfds[i].fp.get()].data;
                    events[remain].events =
                            events_poll_to_epoll(pollfds[i].revents);
                    trace_epoll_ready(pollfds[i].fp.get(), pollfds[i].revents);
                }
            }
        }
        return r;
    }
private:
    void remove_me(file* fp) {
        WITH_LOCK(fp->f_lock) {
            auto i = boost::range::find(*fp->f_epolls, epoll_fp);
            assert(i != fp->f_epolls->end());
            fp->f_epolls->erase(i);
        }
    }
};

static int epoll_fop_init(file* f)
{
    return 0;
}

static int epoll_fop_close(file *f)
{
    delete static_cast<epoll_obj*>(f->f_data);
    f->f_data = nullptr;
    return 0;
}

static fileops epoll_ops = {
    epoll_fop_init,
    unsupported_read,
    unsupported_write,
    unsupported_truncate,
    unsupported_ioctl,
    unsupported_poll,
    unsupported_stat,
    epoll_fop_close,
    unsupported_chmod,
};

static inline bool is_epoll(struct file *f)
{
    return f->f_ops == &epoll_ops;
}


static inline epoll_obj *get_epoll_obj(fileref fr) {
    struct file *f = fr.get();
    if (is_epoll(f)) {
        return static_cast<epoll_obj*> (f->f_data);
    } else {
        return nullptr;
    }
}

int epoll_create(int size)
{
    // Note we ignore the size parameter. There's no point in checking it's
    // positive, and on the other hand we can't trust it being meaningful
    // because Linux ignores it too.
    return epoll_create1(0);
}

int epoll_create1(int flags)
{
    flags &= ~EPOLL_CLOEXEC;
    assert(!flags);
    try {
        std::unique_ptr<epoll_obj> s{new epoll_obj};
        fileref f = make_file(0 , DTYPE_UNSPEC, s.get(), &epoll_ops);
        s->set_epoll_fp(f.get());
        s.release();
        fdesc fd(f);
        trace_epoll_create(fd.get());
        return fd.release();
    } catch (int error) {
        errno = error;
        trace_epoll_create(-1);
        return -1;
    }
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    trace_epoll_ctl(epfd, fd,
            op==EPOLL_CTL_ADD ? "EPOLL_CTL_ADD" :
            op==EPOLL_CTL_MOD ? "EPOLL_CTL_MOD" :
            op==EPOLL_CTL_DEL ? "EPOLL_CTL_DEL" : "?");
    fileref epfr(fileref_from_fd(epfd));
    if (!epfr) {
        errno = EBADF;
        return -1;
    }

    epoll_obj *epo = get_epoll_obj(epfr);
    if (!epo) {
        errno = EINVAL;
        return -1;
    }

    int error = 0;
    fileref fp = fileref_from_fd(fd);

    switch (op) {
    case EPOLL_CTL_ADD:
        error = epo->add(fp.get(), event);
        break;
    case EPOLL_CTL_MOD:
        error = epo->mod(fp.get(), event);
        break;
    case EPOLL_CTL_DEL:
        error = epo->del(fp.get());
        break;
    default:
        error = EINVAL;
    }

    if (error) {
        errno = error;
        return -1;
    } else {
        return 0;
    }
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout_ms)
{
    trace_epoll_wait(epfd, maxevents, timeout_ms);
    fileref epfr(fileref_from_fd(epfd));
    if (!epfr) {
        errno = EBADF;
        return -1;
    }

    epoll_obj *epo = get_epoll_obj(epfr);
    if (!epo || maxevents <= 0) {
        errno = EINVAL;
        return -1;
    }

    return epo->wait(events, maxevents, timeout_ms);
}

void epoll_file_closed(file* epoll_file, file* client)
{
    fileref epoll_ptr(epoll_file);
    auto epoll_obj = get_epoll_obj(epoll_ptr);
    epoll_obj->del(client);
}
