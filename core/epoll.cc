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

#include <osv/debug.hh>
#include <unordered_map>
#include <boost/range/algorithm/find.hpp>

#include <osv/trace.hh>
TRACEPOINT(trace_epoll_create, "returned fd=%d", int);
TRACEPOINT(trace_epoll_ctl, "epfd=%d, fd=%d, op=%s", int, int, const char*);
TRACEPOINT(trace_epoll_wait, "epfd=%d, maxevents=%d, timeout=%d", int, int, int);
TRACEPOINT(trace_epoll_ready, "file=%p, event=0x%x", file*, int);

// We implement epoll using poll(), and therefore need to convert epoll's
// event bits to and poll(). These are mostly the same, so the conversion
// is trivial, but we verify this here with static_asserts. We additionally
// support the epoll-only EPOLLET, but not EPOLLONESHOT.
static_assert(POLLIN == EPOLLIN, "POLLIN!=EPOLLIN");
static_assert(POLLOUT == EPOLLOUT, "POLLOUT!=EPOLLOUT");
static_assert(POLLRDHUP == EPOLLRDHUP, "POLLRDHUP!=EPOLLRDHUP");
static_assert(POLLPRI == EPOLLPRI, "POLLPRI!=EPOLLPRI");
static_assert(POLLERR == EPOLLERR, "POLLERR!=EPOLLERR");
static_assert(POLLHUP == EPOLLHUP, "POLLHUP!=EPOLLHUP");
constexpr int SUPPORTED_EVENTS =
        EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLPRI | EPOLLERR | EPOLLHUP |
        EPOLLET;
inline uint32_t events_epoll_to_poll(uint32_t e)
{
    assert (!(e & ~SUPPORTED_EVENTS));
    return e;
}
inline uint32_t events_poll_to_epoll(uint32_t e)
{
    assert (!(e & ~SUPPORTED_EVENTS));
    return e;
}

struct registered_epoll : epoll_event {
    int last_poll_wake_count; // For implementing EPOLLET
    registered_epoll(epoll_event e, int c) :
        epoll_event(e), last_poll_wake_count(c) {}
};

class epoll_file final : public special_file {
    std::unordered_map<epoll_key, registered_epoll> map;
public:
    epoll_file() : special_file(0, DTYPE_UNSPEC) {}
    virtual int close() override {
        for (auto& e : map) {
            remove_me(e.first);
        }
        return 0;
    }
    int add(epoll_key key, struct epoll_event *event)
    {
        WITH_LOCK(f_lock) {
            if (map.count(key)) {
                return EEXIST;
            }
            auto fp = key._file;
            WITH_LOCK(fp->f_lock) {
                // I used poll_wake_count-1, to ensure EPOLLET returns once when
                // registering an epoll after data is already available.
                map.emplace(key,
                        registered_epoll(*event, fp->poll_wake_count - 1));
                if (!fp->f_epolls) {
                    fp->f_epolls.reset(new std::vector<epoll_ptr>);
                }
                fp->f_epolls->push_back(epoll_ptr{this, key});
            }
            return 0;
        }
    }
    int mod(epoll_key key, struct epoll_event *event)
    {
        WITH_LOCK(f_lock) {
            auto fp = key._file;
            try {
                WITH_LOCK(fp->f_lock) {
                    map.at(key) = registered_epoll(*event, fp->poll_wake_count - 1);
                }
                return 0;
            } catch (std::out_of_range &e) {
                return ENOENT;
            }
        }
    }
    int del(epoll_key key)
    {
        WITH_LOCK(f_lock) {
            if (map.erase(key)) {
                remove_me(key);
                return 0;
            } else {
                return ENOENT;
            }
        }
    }
    int wait(struct epoll_event *events, int maxevents, int timeout_ms)
    {
        std::vector<poll_file> pollfds;
        std::vector<epoll_key> keys;
        WITH_LOCK(f_lock) {
            pollfds.reserve(map.size());
            keys.reserve(map.size());
            for (auto &i : map) {
                int eevents = i.second.events;
                auto events = events_epoll_to_poll(eevents);
                pollfds.emplace_back(i.first._file, events, 0,
                        i.second.last_poll_wake_count);
                keys.emplace_back(i.first);
            }
            int r;
            DROP_LOCK(f_lock) {
                r = do_poll(pollfds, parse_poll_timeout(timeout_ms));
            }
            if (r > 0) {
                r = std::min(r, maxevents);
                int remain = r;
                for (size_t i = 0; i < pollfds.size() && remain;  i++) {
                    if (pollfds[i].revents) {
                        --remain;
                        assert(pollfds[i].fp);
                        events[remain].data = map.at(keys[i]).data;
                        events[remain].events =
                                events_poll_to_epoll(pollfds[i].revents);
                        trace_epoll_ready(pollfds[i].fp.get(), pollfds[i].revents);
                        if (pollfds[i].events & EPOLLET) {
                            map.at(keys[i]).last_poll_wake_count =
                                    pollfds[i].last_poll_wake_count;
                        }
                    }
                }
            }
            return r;
        }
    }
private:
    void remove_me(epoll_key key) {
        auto fp = key._file;
        WITH_LOCK(fp->f_lock) {
            epoll_ptr ptr{this, key};
            auto i = boost::range::find(*fp->f_epolls, ptr);
            assert(i != fp->f_epolls->end());
            fp->f_epolls->erase(i);
        }
    }
};

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
        fileref f = make_file<epoll_file>();
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

    auto epo = dynamic_cast<epoll_file*>(epfr.get());
    if (!epo) {
        errno = EINVAL;
        return -1;
    }

    int error = 0;
    fileref fp = fileref_from_fd(fd);
    epoll_key key{fd, fp.get()};

    switch (op) {
    case EPOLL_CTL_ADD:
        error = epo->add(key, event);
        break;
    case EPOLL_CTL_MOD:
        error = epo->mod(key, event);
        break;
    case EPOLL_CTL_DEL:
        error = epo->del(key);
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

    auto epo = dynamic_cast<epoll_file*>(epfr.get());
    if (!epo || maxevents <= 0) {
        errno = EINVAL;
        return -1;
    }

    return epo->wait(events, maxevents, timeout_ms);
}

void epoll_file_closed(epoll_ptr ptr)
{
    auto epoll_obj = dynamic_cast<epoll_file*>(ptr.epoll_file);
    epoll_obj->del(ptr.key);
}
