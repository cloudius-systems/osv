/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/eventfd.h>
#include <fs/fs.hh>
#include <libc/libc.hh>
#include <osv/fcntl.h>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/poll.h>

class event_fd final : public special_file {
    public:
        event_fd(unsigned int initval, int is_semaphore,  int flags)
            :special_file(FREAD | FWRITE | flags, DTYPE_UNSPEC),
            _count(initval), _is_semaphore(is_semaphore)
        {}

        int close() { return 0; }

        virtual int read(struct uio *uio, int flags) override;
        virtual int write(struct uio *uio, int flags) override;
        virtual int poll(int events) override;

    private:
        mutable mutex _mutex;
        uint64_t      _count;
        bool          _is_semaphore;
        condvar       _blocked_reader;
        condvar       _blocked_writer;

    private:
        size_t copy_to_uio(uint64_t value, uio *uio);
        size_t copy_from_uio(uio *uio, uint64_t *value);
};

size_t event_fd::copy_to_uio(uint64_t value, uio *uio)
{
    const char   *q = (const char *) &value;
    struct iovec *iov;
    size_t       n;
    char         *p;

    size_t sz = sizeof(value);
    size_t bc = 0;
    for (int i = 0; i < uio->uio_iovcnt && sz != 0; i++) {
        iov = uio->uio_iov + i;
        n   = std::min(sz, iov->iov_len);
        p   = static_cast<char *> (iov->iov_base);

        std::copy(q, q + n, p);

        q  += n;
        sz -= n;
        bc += n;
    }
    return bc;
}

size_t event_fd::copy_from_uio(uio *uio, uint64_t *value)
{
    char         *q = (char *) value;
    struct iovec *iov;
    size_t       n;
    char         *p;

    size_t sz = sizeof(*value);
    size_t bc = 0;
    for (int i = 0; i < uio->uio_iovcnt && sz != 0; i++) {
        iov = uio->uio_iov + i;
        n   = std::min(sz, iov->iov_len);
        p   = static_cast<char *> (iov->iov_base);

        std::copy(p, p + n, q);

        q  += n;
        sz -= n;
        bc += n;
    }
    return bc;
}

int event_fd::read(uio *data, int flags)
{
    uint64_t v;

    if (data->uio_resid < (ssize_t) sizeof(v)) {
        return EINVAL;
    }

    WITH_LOCK(_mutex) {
        for (;;) {
            if (_count > 0) {
                if (_is_semaphore) {
                    v = 1;
                } else {
                    v = _count;
                }
                _count -= v;
                break;
            } else {
                if (f_flags & O_NONBLOCK) {
                    return EAGAIN;
                }
                _blocked_reader.wait(_mutex);
            }
        }

        data->uio_resid -= copy_to_uio(v, data);
        _blocked_writer.wake_all();
    }
    poll_wake(this, POLLOUT);

    return 0;
}

int event_fd::write(uio *data, int flags)
{
    uint64_t v;

    if (data->uio_resid < (ssize_t) sizeof(v)) {
        return EINVAL;
    }

    size_t bc = copy_from_uio(data, &v);
    if (v == ULLONG_MAX) {
        return EINVAL;
    }

    WITH_LOCK(_mutex) {
        for (;;) {
            if (v < ULLONG_MAX - _count) {
                _count += v;
                break;
            } else {
                if (f_flags & O_NONBLOCK) {
                    return EAGAIN;
                }
                _blocked_writer.wait(_mutex);
            }
        }

        _blocked_reader.wake_all();
    }
    poll_wake(this, POLLIN);

    /* update uio_resid only when count is updated. */
    data->uio_resid -= bc;

    return 0;
}

int event_fd::poll(int events)
{
    int rc = 0;

    WITH_LOCK(_mutex) {
        if ((_count > 0) && ((events & POLLIN) != 0)) {
            /* readable */
            rc |= POLLIN;
        }

        if ((_count < ULLONG_MAX - 1) && ((events & POLLOUT) != 0)) {
            /* writable */
            rc |= POLLOUT;
        }

        if (_count == ULLONG_MAX) {
            /* error on overflow */
            rc |= POLLERR;
        }
    }

    return rc;
}

int eventfd(unsigned int initval, int flags)
{
    if (flags & (~(EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE))) {
        return libc_error(EINVAL);
    }

    if (initval >= ULLONG_MAX) {
        return libc_error(EINVAL);
    }

    int of = 0;
    if (flags & EFD_NONBLOCK) {
        of |= O_NONBLOCK;
    }

    if (flags & EFD_CLOEXEC) {
        of |= O_CLOEXEC;
    }

    bool is_semaphore = (flags & EFD_SEMAPHORE);

    try {
        fileref f = make_file<event_fd>(initval, is_semaphore, of);
        fdesc fd(f);
        return fd.release();
    } catch (int error) {
        return libc_error(error);
    }
}
weak_alias(eventfd, eventfd2);

int eventfd_read(int fd, eventfd_t *value)
{
    fileref f(fileref_from_fd(fd));
    if (!f) {
        return libc_error(EBADF);
    }

    auto ef = dynamic_cast<event_fd *> (f.get());
    if (!ef) {
        return libc_error(EBADF);
    }

    struct iovec iov {value, sizeof(*value)};
    struct uio   uio {&iov, 1, 0, sizeof(*value), UIO_READ};

    int rc = ef->read(&uio, 0);
    if (rc) {
        return libc_error(rc);
    }
    return 0;
}

int eventfd_write(int fd, eventfd_t value)
{
    fileref f(fileref_from_fd(fd));
    if (!f) {
        return libc_error(EBADF);
    }

    auto ef = dynamic_cast<event_fd *> (f.get());
    if (!ef) {
        return libc_error(EBADF);
    }

    struct iovec iov {&value, sizeof(value)};
    struct uio   uio {&iov, 1, 0, sizeof(value), UIO_WRITE};

    int rc = ef->write(&uio, 0);
    if (rc) {
        return libc_error(rc);
    }
    return 0;
}
