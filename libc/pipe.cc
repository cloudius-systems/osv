#include "af_local.h"
#include "pipe_buffer.hh"

#include <fs/unsupported.h>
#include <osv/fcntl.h>
#include <libc/libc.hh>

#include <fcntl.h>
#include <unistd.h>
#include <sys/poll.h>

struct pipe_writer {
    pipe_buffer_ref buf;
    pipe_writer(pipe_buffer *b) : buf(b) { }
    ~pipe_writer() { buf->detach_sender(); }
};

struct pipe_reader {
    pipe_buffer_ref buf;
    pipe_reader(pipe_buffer *b) : buf(b) { }
    ~pipe_reader() { buf->detach_receiver(); }
};

static int pipe_init(file* f)
{
    if (f->f_flags & FWRITE) {
        pipe_writer *po = static_cast<pipe_writer*>(f->f_data);
        po->buf->attach_sender(f);
    } else {
        pipe_reader *po = static_cast<pipe_reader*>(f->f_data);
        po->buf->attach_receiver(f);
    }
    return 0;
}

static int pipe_read(file *f, uio *data, int flags)
{
    pipe_reader *po = static_cast<pipe_reader*>(f->f_data);
    return po->buf->read(data, is_nonblock(f));
}

static int pipe_write(file *f, uio *data, int flags)
{
    pipe_writer *po = static_cast<pipe_writer*>(f->f_data);
    return po->buf->write(data, is_nonblock(f));
}

static int pipe_poll(file *f, int events)
{
    int revents = 0;
    // One end of the pipe is read-only, the other write-only:
    if (f->f_flags & FWRITE) {
        pipe_writer *po = static_cast<pipe_writer*>(f->f_data);
        if (events & POLLOUT) {
            revents |= po->buf->write_events();
        }
    } else {
        pipe_reader *po = static_cast<pipe_reader*>(f->f_data);
        if (events & POLLIN) {
            revents |= po->buf->read_events();
        }
    }
    return revents;
}

static int pipe_close(file *f)
{
    if (f->f_flags & FWRITE) {
        delete static_cast<pipe_writer*>(f->f_data);
    } else {
        delete static_cast<pipe_reader*>(f->f_data);
    }
    f->f_data = nullptr;
    return 0;
}

static fileops pipe_ops = {
    pipe_init,
    pipe_read,
    pipe_write,
    unsupported_truncate,
    unsupported_ioctl,
    pipe_poll,
    unsupported_stat,
    pipe_close,
    unsupported_chmod,
};

int pipe(int pipefd[2]) {
    auto b = new pipe_buffer;
    std::unique_ptr<pipe_reader> s1{new pipe_reader(b)};
    std::unique_ptr<pipe_writer> s2{new pipe_writer(b)};
    try {
        fileref f1{falloc_noinstall()};
        fileref f2{falloc_noinstall()};
        finit(f1.get(), FREAD, DTYPE_UNSPEC, s1.release(), &pipe_ops);
        finit(f2.get(), FWRITE, DTYPE_UNSPEC, s2.release(), &pipe_ops);
        fdesc fd1(f1);
        fdesc fd2(f2);
        // all went well, user owns descriptors now
        pipefd[0] = fd1.release();
        pipefd[1] = fd2.release();
        return 0;
    } catch (int error) {
        return libc_error(error);
    }
}
