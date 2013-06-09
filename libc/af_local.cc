#include "af_local.h"
#include "pipe_buffer.hh"

#include <fs/unsupported.h>
#include <osv/fcntl.h>
#include <libc/libc.hh>

#include <fcntl.h>
#include <sys/socket.h>

struct af_local {
    af_local(pipe_buffer* s, pipe_buffer* r) : send(s), receive(r) {}
    ~af_local() {
        send->detach_sender();
        receive->detach_receiver();
    }
    pipe_buffer_ref send;
    pipe_buffer_ref receive;
};

int af_local_init(file* f)
{
    af_local* afl = static_cast<af_local*>(f->f_data);
    afl->send->attach_sender(f);
    afl->receive->attach_receiver(f);
    return 0;
}

int af_local_read(file* f, uio* data, int flags)
{
    af_local* afl = static_cast<af_local*>(f->f_data);
    return afl->receive->read(data, is_nonblock(f));
}

int af_local_write(file* f, uio* data, int flags)
{
    af_local* afl = static_cast<af_local*>(f->f_data);
    assert(!(f->f_flags & FNONBLOCK));
    return afl->send->write(data, is_nonblock(f));
}

int af_local_poll(file* f, int events)
{
    af_local* afl = static_cast<af_local*>(f->f_data);
    int revents = 0;
    if (events & POLLIN) {
        revents |= afl->receive->read_events();
    }
    if (events & POLLOUT) {
        revents |= afl->send->write_events();
    }
    return revents;
}

int af_local_close(file* f)
{
    auto afl = static_cast<af_local*>(f->f_data);
    delete afl;
    f->f_data = nullptr;
    return 0;
}

fileops af_local_ops = {
    af_local_init,
    af_local_read,
    af_local_write,
    unsupported_truncate,
    unsupported_ioctl,
    af_local_poll,
    unsupported_stat,
    af_local_close,
    unsupported_chmod,
};

int socketpair_af_local(int type, int proto, int sv[2])
{
    assert(type == SOCK_STREAM);
    assert(proto == 0);
    auto b1 = new pipe_buffer;
    auto b2 = new pipe_buffer;
    std::unique_ptr<af_local> s1{new af_local(b1, b2)};
    std::unique_ptr<af_local> s2{new af_local(b2, b1)};
    try {
        fileref f1{falloc_noinstall()};
        fileref f2{falloc_noinstall()};
        finit(f1.get(), FREAD|FWRITE, DTYPE_UNSPEC, s1.release(), &af_local_ops);
        finit(f2.get(), FREAD|FWRITE, DTYPE_UNSPEC, s2.release(), &af_local_ops);
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
    struct file *f = fr.get();
    if (f->f_ops != &af_local_ops) {
        return ENOTSOCK;
    }
    af_local *afl = static_cast<af_local*>(f->f_data);
    switch (how) {
    case SHUT_RD:
        afl->receive->detach_receiver();
        f->f_flags &= ~FREAD;
        break;
    case SHUT_WR:
        afl->send->detach_sender();
        f->f_flags &= ~FWRITE;
        break;
    case SHUT_RDWR:
        afl->receive->detach_receiver();
        afl->send->detach_sender();
        f->f_flags &= ~(FREAD|FWRITE);
        break;
    default:
        return EINVAL;
    }
    return 0;
}
