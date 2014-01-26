/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <fs/fs.hh>
#include <osv/fcntl.h>
#include <libc/libc.hh>
#include <osv/stubbing.hh>
#include <osv/sched.hh>
#include <osv/poll.h>
#include <osv/mutex.h>
#include <osv/condvar.h>

#include <sys/timerfd.h>

#include <atomic>

class timerfd final : public special_file {
public:
    explicit timerfd(int clockid, int flags);
    virtual ~timerfd() override;

    virtual int read(uio* data, int flags) override;
    virtual int poll(int events) override;
    virtual int close() override;
    void set(s64 expiration, s64 interval);
    void get(s64 &expiration, s64 &interval) const;

private:
    mutable mutex _mutex;

    // _expiration is the time when the timerfd becomes readable and read()
    // will return 1. After this time, the value to be returned by read()
    // will increase by 1 on every _interval.
    s64 _expiration = 0;
    s64 _interval = 0;

    // Each timerfd keeps a timer for wakeup of sleeping read() or poll()
    // in a dedicated thread. We could have used a timer_base::client instead
    // of a real thread, but things get really complicated when trying to
    // support set() which cancels on one CPU the timer set on another CPU.
    sched::thread _wakeup_thread;
    s64 _wakeup_due = 0;
    condvar _wakeup_change_cond;
    bool _wakeup_thread_exit = false;
    condvar _blocked_reader;
    void wakeup_thread_func();

    // Which clock to use to interpret s64 times.
    int _clockid;
    void set_timer(sched::timer &tmr, s64 time);
public:
    s64 time_now() const;
};

timerfd::timerfd(int clockid, int oflags)
    : special_file(FREAD | oflags, DTYPE_UNSPEC),
      _wakeup_thread(
            [&] { wakeup_thread_func(); }, sched::thread::attr().stack(4096)),
      _clockid(clockid)
{
    _wakeup_thread.start();
}

timerfd::~timerfd() {
    WITH_LOCK(_mutex) {
        _wakeup_thread_exit = true;
        _wakeup_change_cond.wake_one();
    }
    _wakeup_thread.join();
}

void timerfd::set_timer(sched::timer &tmr, s64 t)
{
    using namespace osv::clock;
    switch(_clockid) {
    case CLOCK_REALTIME:
        tmr.set(wall::time_point(std::chrono::nanoseconds(t)));
        break;
    case CLOCK_MONOTONIC:
        tmr.set(uptime::time_point(std::chrono::nanoseconds(t)));
        break;
    default:
        assert(false);
    }
}

s64 timerfd::time_now() const
{
    using namespace std::chrono;
    switch(_clockid) {
    case CLOCK_REALTIME:
        return duration_cast<nanoseconds>(
                osv::clock::wall::now().time_since_epoch()).count();
    case CLOCK_MONOTONIC:
        return duration_cast<nanoseconds>(
                osv::clock::uptime::now().time_since_epoch()).count();
    default:
        assert(false);
    }
}

void timerfd::wakeup_thread_func()
{
    sched::timer tmr(*sched::thread::current());
    WITH_LOCK(_mutex) {
        while (!_wakeup_thread_exit) {
            if (_wakeup_due != 0) {
                set_timer(tmr, _wakeup_due);
                _wakeup_change_cond.wait(_mutex, &tmr);
                if (tmr.expired()) {
                    _wakeup_due = 0;
                    // Wake blocked read() or poll() on this fd
                    _blocked_reader.wake_one();
                    poll_wake(this, POLLIN);
                } else {
                    tmr.cancel();
                }
            } else {
                _wakeup_change_cond.wait(_mutex);
            }
        }
    }
}

void timerfd::set(s64 expiration, s64 interval)
{
    WITH_LOCK(_mutex) {
        _expiration = expiration;
        _interval = interval;
        _wakeup_due = expiration;
        _wakeup_change_cond.wake_one();
        _blocked_reader.wake_one();
    }
}

void timerfd::get(s64 &expiration, s64 &interval) const
{
    WITH_LOCK(_mutex) {
        if (_expiration && !_wakeup_due) {
            // already expired, calculate the time of the next expiration
            if (!_interval) {
                expiration = 0;
            } else {
                auto now = time_now();
                u64 count = (now - _expiration) / _interval;
                expiration = _expiration + (count+1) * _interval;
            }
        } else {
            expiration = _expiration;
        }
        interval = _interval;
    }
}

// Copy from the byte array into the given iovec array, stopping when either
// the array or iovec runs out. Decrements uio->uio_resid.
static void copy_to_uio(const char *q, size_t qlen, uio *uio)
{
    for (int i = 0; i < uio->uio_iovcnt && qlen; i++) {
        auto &iov = uio->uio_iov[i];
        auto n = std::min(qlen, iov.iov_len);
        char* p = static_cast<char*>(iov.iov_base);
        std::copy(q, q + n, p);
        q += n;
        qlen -= n;
        uio->uio_resid -= n;
    }
}

int timerfd::read(uio *data, int flags)
{
    u64 ret;

    if (data->uio_resid < (ssize_t) sizeof(ret)) {
        return EINVAL;
    }

    WITH_LOCK(_mutex) {
        while (!_expiration || _wakeup_due) {
            if (f_flags & O_NONBLOCK) {
                return EAGAIN;
            }
            _blocked_reader.wait(_mutex);
        }
        // Read the timerfd's current count of expirations since the last
        // read() or set(). If an interval is set, also set a timer until
        // the next expiration.
        if (!_interval) {
            ret = 1;
            _expiration = 0;
        } else {
            auto now = time_now();
            // set next wakeup for the next multiple of interval from
            // _expiration which is after "now".
            assert (now >= _expiration);
            u64 count = (now - _expiration) / _interval;
            _expiration = _expiration + (count+1) * _interval;
            _wakeup_due = _expiration;
            _wakeup_change_cond.wake_one();
            ret = 1 + count;
        }
        copy_to_uio((const char *)&ret, sizeof(ret), data);
        return 0;
    }
}

int timerfd::poll(int events)
{
    WITH_LOCK(_mutex) {
        if (!_expiration || _wakeup_due) {
            return 0;
        } else {
            return POLLIN;
        }
    }
}

int timerfd::close()
{
    // No need to do anything explicit - close will delete this object,
    // causing all its fields to be properly destructed.
    return 0;
}

// After this long introduction, without further ado, let's implement Linux's
// three <sys/timerfd.h> functions:

int timerfd_create(int clockid, int flags) {
    switch (clockid) {
    case CLOCK_REALTIME:
    case CLOCK_MONOTONIC:
        // fine.
        break;
    default:
        return libc_error(EINVAL);
    }
    if (flags & ~(TFD_NONBLOCK | TFD_CLOEXEC)) {
        return libc_error(EINVAL);
    }
    try {
        int oflags = (flags & TFD_NONBLOCK) ? O_NONBLOCK : 0;
        fileref f = make_file<timerfd>(clockid, oflags);
        fdesc fd(f);
        return fd.release();
    } catch (int error) {
        return libc_error(error);
    }
}

static constexpr s64 second = 1000000000;

int timerfd_settime(int fd, int flags, const itimerspec *newval,
        itimerspec *oldval)
{
    fileref f(fileref_from_fd(fd));
    if (!f) {
        errno = EBADF;
        return -1;
    }
    auto tf = dynamic_cast<timerfd*>(f.get());
    if (!tf) {
        errno = EINVAL;
        return -1;
    }

    s64 expiration, interval;
    auto now = tf->time_now();
    if (oldval) {
        tf->get(expiration, interval);
        if (expiration) {
            // oldval is always returned in relative time
            expiration -= now;
        }
        oldval->it_value.tv_sec = expiration / second;
        oldval->it_value.tv_nsec = expiration % second;
        oldval->it_interval.tv_sec = interval / second;
        oldval->it_interval.tv_nsec = interval % second;
    }

    expiration = newval->it_value.tv_sec * second + newval->it_value.tv_nsec;
    interval = newval->it_interval.tv_sec * second + newval->it_interval.tv_nsec;
    if (flags != TFD_TIMER_ABSTIME && expiration) {
        expiration += now;
    }

    tf->set(expiration, interval);
    return 0;
}

int timerfd_gettime(int fd, itimerspec *val)
{
    fileref f(fileref_from_fd(fd));
    if (!f) {
        errno = EBADF;
        return -1;
    }
    auto tf = dynamic_cast<timerfd*>(f.get());
    if (!tf) {
        errno = EINVAL;
        return -1;
    }

    s64 expiration, interval;
    auto now = tf->time_now();
    tf->get(expiration, interval);
    if (expiration) {
        // timerfd_gettime() wants relative time
        expiration -= now;
    }
    val->it_value.tv_sec = expiration / second;
    val->it_value.tv_nsec = expiration % second;
    val->it_interval.tv_sec = interval / second;
    val->it_interval.tv_nsec = interval % second;
    return 0;
}
