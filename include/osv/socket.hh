/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SOCKET_HH_
#define SOCKET_HH_

#include <osv/file.h>
#include <memory>

struct socket;
struct socket_closer;

extern "C" int soclose(socket* so);

struct socket_closer {
        void operator()(socket* so) { soclose(so); }
};

using socketref = std::unique_ptr<socket, socket_closer>;

class socket_file final : public file {
public:
    socket_file(unsigned flags, socket* so);
    socket_file(unsigned flags, socketref&& so);
    virtual int read(struct uio *uio, int flags) override;
    virtual int write(struct uio *uio, int flags) override;
    virtual int truncate(off_t len) override;
    virtual int ioctl(u_long com, void *data) override;
    virtual int poll(int events) override;
    virtual int stat(struct stat* buf) override;
    virtual int close() override;
    virtual int chmod(mode_t mode) override;
    socket* so;
};

#endif /* SOCKET_HH_ */
