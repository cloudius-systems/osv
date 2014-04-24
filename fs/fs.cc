/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "fs.hh"
#include <fcntl.h>
#include <sys/stat.h>
#include <osv/error.h>

uint64_t size(fileref f)
{
    struct stat st;
    int r = f.get()->stat(&st);
    if (r) {
        throw make_error(r);
    }
    return st.st_size;
}

void read(fileref f, void *buffer, uint64_t offset, uint64_t len)
{
    iovec iov{buffer, len};
    // FIXME: breaks on 32-bit
    uio data{&iov, 1, off_t(offset), ssize_t(len), UIO_READ};
    int r = f.get()->read(&data, FOF_OFFSET);
    if (r) {
        throw make_error(r);
    }
    // FIXME: uio_resid!=0 is possible on a non-blocking file.
    // We should improve the read() API to say how much was read...
    assert(data.uio_resid == 0);
}

void write(fileref f, const void* buffer, uint64_t offset, uint64_t len)
{
    iovec iov{const_cast<void*>(buffer), len};
    // FIXME: breaks on 32-bit
    uio data{&iov, 1, off_t(offset), ssize_t(len), UIO_WRITE};
    int r = f.get()->write(&data, FOF_OFFSET);
    if (r) {
        throw make_error(r);
    }
    // FIXME: uio_resid!=0 is possible on a non-blocking file.
    // We should improve the write() API to say how much was written...
    assert(data.uio_resid == 0);
}

fileref fileref_from_fd(int fd)
{
    file* fp;
    if (fget(fd, &fp) == 0) {
        return fileref(fp, false);
    } else {
        return fileref();
    }
}

fileref fileref_from_fname(std::string fname)
{
    int fd = open(fname.c_str(), O_RDONLY);
    if (fd == -1) {
        return fileref();
    }
    auto f = fileref_from_fd(fd);
    close(fd);
    return f;
}

fdesc::fdesc(file* f)
{
    int r = fdalloc(f, &_fd);
    if (r) {
        // FIXME: do throw make_error(r);
        throw r;
    }
}

fdesc::~fdesc()
{
    if (_fd != -1) {
        close(_fd);
    }
}
