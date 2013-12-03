/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "af_local.h"
#include "pipe_buffer.hh"

#include <fs/fs.hh>
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

class pipe_file final : public file {
public:
    explicit pipe_file(std::unique_ptr<pipe_reader>&& s);
    explicit pipe_file(std::unique_ptr<pipe_writer>&& s);
    virtual int read(uio* data, int flags) override;
    virtual int write(uio* data, int flags) override;
    virtual int poll(int events) override;
    virtual int close() override;
    virtual int truncate(off_t len) override { return unsupported_truncate(this, len); }
    virtual int ioctl(ulong com, void* data) override { return unsupported_ioctl(this, com, data); }
    virtual int stat(struct stat* buf) override { return unsupported_stat(this, buf); }
    virtual int chmod(mode_t mode) override { return unsupported_chmod(this, mode); }
private:
    pipe_writer* writer = nullptr;
    pipe_reader* reader = nullptr;
};

pipe_file::pipe_file(std::unique_ptr<pipe_writer>&& s)
    : file(FWRITE, DTYPE_UNSPEC)
    , writer(s.release())
{
    writer->buf->attach_sender(this);
}

pipe_file::pipe_file(std::unique_ptr<pipe_reader>&& s)
    : file(FREAD, DTYPE_UNSPEC)
    , reader(s.release())
{
    reader->buf->attach_receiver(this);
}

int pipe_file::read(uio *data, int flags)
{
    return reader->buf->read(data, is_nonblock(this));
}

int pipe_file::write(uio *data, int flags)
{
    return writer->buf->write(data, is_nonblock(this));
}

int pipe_file::poll(int events)
{
    int revents = 0;
    // One end of the pipe is read-only, the other write-only:
    if (f_flags & FWRITE) {
        if (events & POLLOUT) {
            revents |= writer->buf->write_events();
        }
    } else {
        if (events & POLLIN) {
            revents |= reader->buf->read_events();
        }
    }
    return revents;
}

int pipe_file::close()
{
    if (f_flags & FWRITE) {
        delete writer;
        writer = nullptr;
    } else {
        delete reader;
        reader = nullptr;
    }
    return 0;
}

int pipe(int pipefd[2]) {
    auto b = new pipe_buffer;
    std::unique_ptr<pipe_reader> s1{new pipe_reader(b)};
    std::unique_ptr<pipe_writer> s2{new pipe_writer(b)};
    try {
        fileref f1 = make_file<pipe_file>(move(s1));
        fileref f2 = make_file<pipe_file>(move(s2));
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
