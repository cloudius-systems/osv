/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 * Copyright (C) 2024 OSv Contributors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "crucible-connection.hh"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <algorithm>
#include <errno.h>
#include <cstring>
#include <stdexcept>

namespace crucible {

Connection::Connection(const std::string& host, uint16_t port,
                       const std::atomic<bool>& cancelled, Deadline admission_deadline)
    : cancelled_(cancelled), admission_deadline_(admission_deadline), host_(host), port_(port)
{
    // Numeric IPv4 only: synchronous DNS has no portable cancellation contract.
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (!port || inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        throw ConnectionError("Crucible requires a numeric IPv4 address and nonzero port");
    }
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        throw ConnectionError("Failed to create socket");
    }
    try {
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            throw ConnectionError("Cannot make socket nonblocking");
        }
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            if (errno != EINPROGRESS) {
                throw ConnectionError("Connect failed");
            }
            wait_ready(POLLOUT, admission_deadline_);
            int error = 0;
            socklen_t size = sizeof(error);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error) {
                throw ConnectionError("Connect failed");
            }
        }
        if (cancelled_ || std::chrono::steady_clock::now() >= admission_deadline_) {
            throw ConnectionError("Admission cancelled or expired");
        }
    } catch (...) {
        ::close(fd_);
        fd_ = -1;
        throw;
    }

    // Disable Nagle: Crucible's synchronous write/flush path sends one small
    // frame and blocks for the ack, so Nagle + delayed-ACK adds ~14 ms per
    // round-trip (0.6 MB/s small writes). TCP_NODELAY pushes each frame
    // immediately.
    int opt = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    // Enable TCP keepalive so the OS detects a dead downstairs connection
    // and unblocks blocked recv() calls rather than hanging indefinitely.
    opt = 1;
    setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

    // First keepalive probe after 10 s of inactivity.
    opt = 10;
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPIDLE, &opt, sizeof(opt));

    // Subsequent probes every 5 s.
    opt = 5;
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPINTVL, &opt, sizeof(opt));

    // Declare the connection dead after 3 unanswered probes (≈25 s total).
    opt = 3;
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPCNT, &opt, sizeof(opt));

    connected_ = true;
}

Connection::~Connection()
{
    close();
}

void Connection::shutdown()
{
    // fd_ is immutable until all users have joined.
    connected_ = false;
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
    }
}

ssize_t Connection::recv_available(void* buf, size_t len)
{
    ssize_t n;
    do {
        n = ::recv(fd_, buf, len, MSG_DONTWAIT);
    } while (n < 0 && errno == EINTR);
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        throw ConnectionError("Receive failed");
    }
    return n;
}

ssize_t Connection::send(const void* buf, size_t len)
{
    if (!connected_) {
        throw ConnectionError("Not connected");
    }

    ssize_t sent = ::send(fd_, buf, len, MSG_NOSIGNAL);
    if (sent < 0) {
        int saved_errno = errno;
        throw ConnectionError("Send failed: " + std::string(strerror(saved_errno)));
    }

    return sent;
}

ssize_t Connection::recv(void* buf, size_t len)
{
    if (!connected_) {
        throw ConnectionError("Not connected");
    }

    ssize_t received = ::recv(fd_, buf, len, 0);
    if (received < 0) {
        int saved_errno = errno;
        throw ConnectionError("Recv failed: " + std::string(strerror(saved_errno)));
    }


    return received;
}

void Connection::wait_ready(short events, Deadline deadline)
{
    while (!cancelled_) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            throw ConnectionError("Socket operation deadline exceeded");
        }
        pollfd pfd{fd_, events, 0};
        int result = poll(&pfd, 1, std::min<int64_t>(remaining, 50));
        if (result > 0) {
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                throw ConnectionError("Socket closed");
            }
            if (pfd.revents & events) {
                return;
            }
        } else if (result < 0 && errno != EINTR) {
            throw ConnectionError("Socket poll failed");
        }
    }
    throw ConnectionError("Socket operation cancelled");
}

void Connection::send_all_locked(const void* buf, size_t len, Deadline deadline)
{
    auto ptr = static_cast<const uint8_t*>(buf);
    while (len) {
        wait_ready(POLLOUT, deadline);
        ssize_t n = ::send(fd_, ptr, len, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;
        }
        if (n <= 0) {
            throw ConnectionError("Send failed");
        }
        ptr += n;
        len -= n;
    }
}

void Connection::send_exact(const void* buf, size_t len)
{
    std::lock_guard<std::mutex> guard(send_lock_);
    send_all_locked(buf, len, std::chrono::steady_clock::now() + std::chrono::seconds(5));
}

void Connection::send_exact(const void* buf, size_t len, Deadline deadline)
{
    std::lock_guard<std::mutex> guard(send_lock_);
    send_all_locked(buf, len, std::min(deadline,
        std::chrono::steady_clock::now() + std::chrono::seconds(5)));
}

void Connection::send_exact_with_data(const void* header, size_t hlen,
                                      const void* data, size_t dlen)
{
    std::lock_guard<std::mutex> guard(send_lock_);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    send_all_locked(header, hlen, deadline);
    send_all_locked(data, dlen, deadline);
}

void Connection::recv_exact(void* buf, size_t len)
{
    std::lock_guard<std::mutex> guard(recv_lock_);
    auto ptr = static_cast<uint8_t*>(buf);
    while (len) {
        wait_ready(POLLIN, admission_deadline_);
        ssize_t n = ::recv(fd_, ptr, len, MSG_DONTWAIT);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;
        }
        if (n <= 0) {
            throw ConnectionError("Receive failed");
        }
        ptr += n;
        len -= n;
    }
}

bool Connection::is_connected() const
{
    return connected_;
}

void Connection::close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    connected_ = false;
}

} // namespace crucible
