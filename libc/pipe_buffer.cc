// Implement a pipe-like buffer, used for implementing both Posix-like pipes
// and bidirectional pipes (unix-domain stream socketpair).

#include "pipe_buffer.hh"

#include <osv/poll.h>

void pipe_buffer::detach_sender()
{
    std::lock_guard<mutex> guard(mtx);
    if (sender) {
        sender = nullptr;
        if (receiver)
            poll_wake(receiver, POLLRDHUP);
        may_read.wake_all();
    }
}

void pipe_buffer::detach_receiver()
{
    std::lock_guard<mutex> guard(mtx);
    if (receiver) {
        receiver = nullptr;
        if (sender)
            poll_wake(sender, POLLHUP);
        may_write.wake_all();
    }
}

void pipe_buffer::attach_sender(struct file *f)
{
    assert(sender == nullptr);
    sender = f;
}

void pipe_buffer::attach_receiver(struct file *f)
{
    assert(receiver == nullptr);
    receiver = f;
}

int pipe_buffer::read_events_unlocked()
{
    int ret = 0;
    ret |= !q.empty() ? POLLIN : 0;
    ret |= !sender ? POLLRDHUP : 0;
    return ret;
}

int pipe_buffer::write_events_unlocked()
{
    if (!receiver) {
        return POLLHUP;
    }
    int ret = 0;
    ret |= q.size() < max_buf ? POLLOUT : 0;
    return ret;
}

int pipe_buffer::read_events()
{
    return with_lock(mtx, [=] { return read_events_unlocked(); });
}

int pipe_buffer::write_events()
{
    return with_lock(mtx, [=] { return write_events_unlocked(); });
}

// Copy from the pipe into the given iovec array, until the array is full
// or the queue is empty. Decrements uio->uio_resid.
static void copy_to_uio(std::deque<char> &q, uio *uio)
{
    for (int i = 0; i < uio->uio_iovcnt && !q.empty(); i++) {
        auto &iov = uio->uio_iov[i];
        auto n = std::min(q.size(), iov.iov_len);
        char* p = static_cast<char*>(iov.iov_base);
        std::copy(q.begin(), q.begin() + n, p);
        q.erase(q.begin(), q.begin() + n);
        uio->uio_resid -= n;
    }
}

int pipe_buffer::read(uio* data)
{
    if (!data->uio_resid) {
        return 0;
    }
    with_lock(mtx, [&] {
        int r;
        while ((r = read_events_unlocked()) == 0) {
            may_read.wait(&mtx);
        }
        if (!(r & POLLIN)) {
            assert(r & POLLRDHUP);
            return;
        }
        copy_to_uio(q, data);
        if (write_events_unlocked() & POLLOUT)
            poll_wake(sender, (POLLOUT | POLLWRNORM));
    });
    may_write.wake_all();
    return 0;
}

// Copy from a certain iovec array into a pipe, starting at a given index
// and offset, until the buffer or the array ends. Decrements uio->uio_resid,
// and modifies ind and offset to where the copy stopped.
static void copy_from_uio(uio *uio, size_t *ind, size_t *offset,
        std::deque<char> &buf, size_t bufsize)
{
    int i = *ind;
    size_t off = *offset;

    while (i < uio->uio_iovcnt && buf.size() < bufsize) {
        auto &iov = uio->uio_iov[i];
        auto n = std::min(bufsize - buf.size(), iov.iov_len - off);
        char* p = static_cast<char*>(iov.iov_base) + off;
        std::copy(p, p + n, std::back_inserter(buf));
        uio->uio_resid -= n;
        off += n;
        if (off == iov.iov_len) {
            ++i;
            off = 0;
        }
    }

    *offset = off;
    *ind = i;
}

int pipe_buffer::write(uio* data)
{
    if (!data->uio_resid) {
        return 0;
    }
    int err = 0;
    with_lock(mtx, [&] {
        // FIXME: Should support also non-blocking operation (O_NONBLOCK).
        // A write() smaller than PIPE_BUF (=4096 in Linux) will not be split
        // (i.e., will be "atomic"): For such a small write, we need to wait
        // until there's enough room for all it in the buffer.
        int needroom = data->uio_resid <= 4096 ? data->uio_resid : 1;
        while (receiver && q.size() + needroom > max_buf) {
            may_write.wait(&mtx);
        }

        if (!receiver) {
            // FIXME: If we don't generate a SIGPIPE here, at least assert
            // that SIGPIPE is SIG_IGN (which can be our default); If the
            // user installed
            err = EPIPE;
            return;
        }

        // A blocking write() to a pipe never returns with partial success -
        // it waits, possibly writing its output in parts and waiting multiple
        // times, until the whole given buffer is written.
        size_t ind = 0, offset = 0;
        while (data->uio_resid && receiver) {
            copy_from_uio(data, &ind, &offset, q, max_buf);
            if (data->uio_resid) {
                // The buffer is full but we still have more to send. Wake up
                // readers, and go to sleep ourselves.
                assert(q.size() == max_buf);
                poll_wake(receiver, (POLLIN | POLLRDNORM));
                may_read.wake_all();
                while (receiver && q.size() == max_buf) {
                    may_write.wait(&mtx);
                }
            }
        }
        if (read_events_unlocked() & POLLIN)
            poll_wake(receiver, (POLLIN | POLLRDNORM));
    });
    may_read.wake_all();
    return err;
}
