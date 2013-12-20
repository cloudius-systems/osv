/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <fs/fs.hh>
#include <fs/unsupported.h>
#include <osv/fcntl.h>
#include <libc/libc.hh>
#include <osv/stubbing.hh>
#include <sched.hh>
#include <drivers/clock.hh>
#include <osv/poll.h>
#include <osv/mutex.h>
#include <osv/condvar.h>

#include <sys/timerfd.h>

#include <atomic>

class timerfd_object {
private:
    // Whom to wake when the timerfd becomes readable. This is a thread
    // waiting on read(), or a null thread if nobody is read()ing.
    sched::thread_handle &_h;
    // Which fd's poll_wake() to call when the timerfd becomes readable.
    file *_f;

    // _expiration is the time when the timerfd becomes readable and read()
    // will return 1. After this time, the value to be returned by read()
    // will increase by 1 on every _interval.
    s64 _expiration = 0;
    s64 _interval = 0;

    // Whether _expiration has already passed, we woke _h and _f already,
    // and new readers or pollers will not block.
    std::atomic<bool> _expired {false};

    // Each timerfd keeps a timer for wakeup of _h and _f (as explained above)
    // in a dedicated thread. We could have used a timer_base::client instead
    // of a real thread, but things get really complicated when trying to
    // support set() which cancels on one CPU the timer set on another CPU.
    sched::thread _wakeup_thread;
    mutex _mutex;
    s64 _wakeup_due = 0;
    condvar _wakeup_change_cond;
    bool _wakeup_thread_exit = false;
    void wakeup_thread_func()
    {
        sched::timer tmr(*sched::thread::current());
        while (true) {
            WITH_LOCK(_mutex) {
                if (_wakeup_thread_exit) {
                    return;
                }
                if (_wakeup_due != 0) {
                    tmr.set(_wakeup_due);
                    _wakeup_change_cond.wait(_mutex, &tmr);
                    if (tmr.expired()) {
                        _expired.store(true, std::memory_order_relaxed);
                        _wakeup_due = 0;
                        // Possibly wake one ongoing read()
                        _h.wake();
                        // Or wake anybody polling on the fd containing this
                        poll_wake(_f, POLLIN);
                    } else {
                        tmr.cancel();
                    }
                } else {
                    _wakeup_change_cond.wait(_mutex);
                }
            }
        }
    }


public:
    timerfd_object(sched::thread_handle &h, file *f)
        : _h(h), _f(f),  _wakeup_thread(
                [&] { wakeup_thread_func(); }, sched::thread::attr(4096))
    {
        _wakeup_thread.start();
    }

    ~timerfd_object() {
        // Ask the wakeup_thread_func to end, so it can be joined without hang
        WITH_LOCK(_mutex) {
            _wakeup_thread_exit = true;
            _wakeup_change_cond.wake_one();
        }
        _wakeup_thread.join();
    }

    void set(s64 expiration, s64 interval) {
        WITH_LOCK(_mutex) {
            _expired.store(false, std::memory_order_relaxed);
            _expiration = expiration;
            _interval = interval;
            _wakeup_due = expiration;
            _wakeup_change_cond.wake_one();
        }
    }

    void get(s64 &expiration, s64 &interval) const {
        if (is_readable()) {
            // already expired, calculate the time of the next expiration
            if (!_interval) {
                expiration = 0;
            } else {
                auto now = nanotime();
                u64 count = (now - _expiration) / _interval;
                expiration = _expiration + (count+1) * _interval;
            }
        } else {
            expiration = _expiration;
        }
        interval = _interval;
    }

    // Returns true if a poll on the timerfd will return POLLIN, i.e, an
    // expiration has occurred since the last set() or read_and_zero_counter().
    bool is_readable() const {
        return _expired.load(std::memory_order_relaxed);
    }

    // Read the timerfd's current count of expirations since the last read()
    // or set(). May also set a timer until is_readable() becomes true again.
    u64 read_and_zero_counter() {
        if (!_expired.load(std::memory_order_relaxed)) {
            return 0;
        }
        WITH_LOCK(_mutex) {
            // After a read, is_readable() no longer true until next timer.
            _expired.store(false, std::memory_order_relaxed);
            if (!_interval) {
                return 1;
            }
            auto now = nanotime();
            // set next wakeup for the next multiple of interval from
            // _expiration which is after "now".
            assert (now >= _expiration);
            u64 count = (now - _expiration) / _interval;
            _expiration = _expiration + (count+1) * _interval;
            _wakeup_due = _expiration;
            _wakeup_change_cond.wake_one();
            return 1 + count;
        }
    }
};


class timerfd_file final : public file {
public:
    explicit timerfd_file(int clockid, int flags);
    virtual ~timerfd_file() {}
    virtual int read(uio* data, int flags) override;
    virtual int poll(int events) override;
    virtual int close() override;

    virtual int write(uio* data, int flags) override { return unsupported_write(this, data, flags); }
    virtual int truncate(off_t len) override { return unsupported_truncate(this, len); }
    virtual int ioctl(ulong com, void* data) override { return unsupported_ioctl(this, com, data); }
    virtual int stat(struct stat* buf) override { return unsupported_stat(this, buf); }
    virtual int chmod(mode_t mode) override { return unsupported_chmod(this, mode); }

    sched::thread_handle _h;
    timerfd_object _obj;
    mutex _read_mutex;
};

timerfd_file::timerfd_file(int clockid, int oflags)
    : file(FREAD | oflags, DTYPE_UNSPEC), _h(), _obj(_h, this)
{
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

int timerfd_file::read(uio *data, int flags)
{
    WITH_LOCK(_read_mutex) {
        if (data->uio_resid < (ssize_t) sizeof(decltype(_obj.read_and_zero_counter()))) {
            return EINVAL;
        }
        if (!_obj.is_readable()) {
            if (f_flags & O_NONBLOCK) {
                return EAGAIN;
            }
            // Wait until the timer expires
            _h.reset(*sched::thread::current());
            sched::thread::wait_until([&] { return _obj.is_readable(); });
            _h.clear();
        }
        auto c = _obj.read_and_zero_counter();
        copy_to_uio((const char *)&c, sizeof(c), data);
        return 0;
    }
}

int timerfd_file::poll(int events)
{
    return _obj.is_readable() ? POLLIN : 0;
}

int timerfd_file::close()
{
    // No need to do anything explicit - close will delete this object,
    // causing all its fields to be properly destructed.
    return 0;
}

// After this long introduction, without further ado, let's implement Linux's
// three <sys/timerfd.h> functions:

int timerfd_create(int clockid, int flags) {
    switch (clockid) {
    case CLOCK_MONOTONIC:
        WARN_ONCE("timerfd_create() does not yet support CLOCK_MONOTONIC");
        return libc_error(EINVAL);
    case CLOCK_REALTIME:
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
        fileref f = make_file<timerfd_file>(clockid, oflags);
        fdesc fd(f);
        return fd.release();
    } catch (int error) {
        return libc_error(error);
    }
}

int timerfd_settime(int fd, int flags, const itimerspec *newval,
        itimerspec *oldval)
{
    fileref f(fileref_from_fd(fd));
    if (!f) {
        errno = EBADF;
        return -1;
    }
    auto tf = dynamic_cast<timerfd_file*>(f.get());
    if (!tf) {
        errno = EINVAL;
        return -1;
    }

    s64 expiration, interval;
    s64 now = nanotime();
    if (oldval) {
        tf->_obj.get(expiration, interval);
        if (expiration) {
            // oldval is always returned in relative time
            expiration -= now;
        }
        oldval->it_value.tv_sec = expiration / 1_s;
        oldval->it_value.tv_nsec = expiration % 1_s;
        oldval->it_interval.tv_sec = interval / 1_s;
        oldval->it_interval.tv_nsec = interval % 1_s;
    }

    expiration = newval->it_value.tv_sec * 1_s + newval->it_value.tv_nsec;
    interval = newval->it_interval.tv_sec * 1_s + newval->it_interval.tv_nsec;
    if (flags != TFD_TIMER_ABSTIME && expiration) {
        expiration += now;
    }

    tf->_obj.set(expiration, interval);
    return 0;
}

int timerfd_gettime(int fd, itimerspec *val)
{
    fileref f(fileref_from_fd(fd));
    if (!f) {
        errno = EBADF;
        return -1;
    }
    auto tf = dynamic_cast<timerfd_file*>(f.get());
    if (!tf) {
        errno = EINVAL;
        return -1;
    }

    s64 expiration, interval;
    s64 now = nanotime();
    tf->_obj.get(expiration, interval);
    if (expiration) {
        // timerfd_gettime() wants relative time
        expiration -= now;
    }
    val->it_value.tv_sec = expiration / 1_s;
    val->it_value.tv_nsec = expiration % 1_s;
    val->it_interval.tv_sec = interval / 1_s;
    val->it_interval.tv_nsec = interval % 1_s;
    return 0;
}
