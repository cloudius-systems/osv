/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "af_local.h"
#include "pipe_buffer.hh"

#include <fs/fs.hh>
#include <osv/socket.hh>
#include <osv/fcntl.h>
#include <libc/libc.hh>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <utility>

using namespace std;

struct af_local final : public special_file {
    af_local(const pipe_buffer_ref& s, const pipe_buffer_ref& r)
            : special_file(FREAD|FWRITE, DTYPE_UNSPEC), send(s), receive(r) { init(); }
    af_local(pipe_buffer_ref&& s, pipe_buffer_ref&& r)
            : special_file(FREAD|FWRITE, DTYPE_UNSPEC), send(std::move(s)), receive(std::move(r)) { init(); }
    void init();
    virtual int read(uio* data, int flags) override;
    virtual int write(uio* data, int flags) override;
    virtual int poll(int events) override;
    virtual int close() override;

    pipe_buffer_ref send;
    pipe_buffer_ref receive;
};

void af_local::init()
{
    send->attach_sender(this);
    receive->attach_receiver(this);
}

int af_local::read(uio* data, int flags)
{
    return receive->read(data, is_nonblock(this));
}

int af_local::write(uio* data, int flags)
{
    assert(!(f_flags & FNONBLOCK));
    return send->write(data, is_nonblock(this));
}

int af_local::poll(int events)
{
    int revents = 0;
    if (events & POLLIN) {
        revents |= receive->read_events();
    }
    if (events & POLLOUT) {
        revents |= send->write_events();
    }
    return revents;
}

int af_local::close()
{
    if (send) {
        send->detach_sender();
    }
    if (receive) {
        receive->detach_receiver();
    }
    send.reset();
    receive.reset();
    return 0;
}

int socketpair_af_local(int type, int proto, int sv[2])
{
    assert(type == SOCK_STREAM);
    assert(proto == 0);
    pipe_buffer_ref b1{new pipe_buffer};
    pipe_buffer_ref b2{new pipe_buffer};
    try {
        fileref f1 = make_file<af_local>(b1, b2);
        fileref f2 = make_file<af_local>(std::move(b2), std::move(b1));
        fdesc fd1(f1);
        fdesc fd2(f2);
        // all went well, user owns descriptors now
        sv[0] = fd1.release();
        sv[1] = fd2.release();
        return 0;
    } catch (int error) {
        return libc_error(error);
    }
}

int shutdown_af_local(int fd, int how) {
    fileref fr(fileref_from_fd(fd));
    if (!fr) {
        return EBADF;
    }
    auto f = dynamic_cast<af_local*>(fr.get());
    if (!f) {
        return ENOTSOCK;
    }
    switch (how) {
    case SHUT_RD:
        f->receive->detach_receiver();
        FD_LOCK(f);
        f->f_flags &= ~FREAD;
        FD_UNLOCK(f);
        break;
    case SHUT_WR:
        f->send->detach_sender();
        FD_LOCK(f);
        f->f_flags &= ~FWRITE;
        FD_UNLOCK(f);
        break;
    case SHUT_RDWR:
        f->receive->detach_receiver();
        f->send->detach_sender();
        FD_LOCK(f);
        f->f_flags &= ~(FREAD|FWRITE);
        FD_UNLOCK(f);
        break;
    default:
        return EINVAL;
    }
    return 0;
}
