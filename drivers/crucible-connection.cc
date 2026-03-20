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
#include <errno.h>
#include <cstring>
#include <stdexcept>

namespace crucible {

Connection::Connection(const std::string& host, uint16_t port)
    : host_(host), port_(port)
{
    // Create socket
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        throw ConnectionError("Failed to create socket: " +
                            std::string(strerror(errno)));
    }

    // Resolve hostname
    struct addrinfo hints{};
    struct addrinfo* result = nullptr;

    hints.ai_family = AF_INET;  // IPv4 for now
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    int rv = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
    if (rv != 0) {
        ::close(fd_);
        fd_ = -1;
        throw ConnectionError("Failed to resolve host: " +
                            std::string(gai_strerror(rv)));
    }

    // Try to connect
    bool connected = false;
    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        if (connect(fd_, rp->ai_addr, rp->ai_addrlen) == 0) {
            connected = true;
            break;
        }
    }

    freeaddrinfo(result);

    if (!connected) {
        int saved_errno = errno;
        ::close(fd_);
        fd_ = -1;
        throw ConnectionError("Failed to connect to " + host + ":" +
                            port_str + ": " + strerror(saved_errno));
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

void Connection::reconnect()
{
    close();

    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        throw ConnectionError("Failed to create socket: " +
                              std::string(strerror(errno)));
    }

    struct addrinfo hints{};
    struct addrinfo *result = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port_);
    int rv = getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &result);
    if (rv != 0) {
        ::close(fd_);
        fd_ = -1;
        throw ConnectionError("Failed to resolve host: " +
                              std::string(gai_strerror(rv)));
    }

    bool ok = false;
    for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        if (::connect(fd_, rp->ai_addr, rp->ai_addrlen) == 0) {
            ok = true;
            break;
        }
    }
    freeaddrinfo(result);

    if (!ok) {
        int saved_errno = errno;
        ::close(fd_);
        fd_ = -1;
        throw ConnectionError("Reconnect failed to " + host_ + ":" +
                              port_str + ": " + strerror(saved_errno));
    }

    // Re-apply TCP_NODELAY and keepalive on the new socket.
    int opt = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    opt = 1;
    setsockopt(fd_, SOL_SOCKET,  SO_KEEPALIVE,  &opt, sizeof(opt));
    opt = 10;
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPIDLE,  &opt, sizeof(opt));
    opt = 5;
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPINTVL, &opt, sizeof(opt));
    opt = 3;
    setsockopt(fd_, IPPROTO_TCP, TCP_KEEPCNT,   &opt, sizeof(opt));

    connected_ = true;
}

Connection::~Connection()
{
    close();
}

Connection::Connection(Connection&& other) noexcept
    : fd_(other.fd_)
    , host_(std::move(other.host_))
    , port_(other.port_)
    , connected_(other.connected_)
{
    other.fd_ = -1;
    other.connected_ = false;
}

Connection& Connection::operator=(Connection&& other) noexcept
{
    if (this != &other) {
        close();

        fd_ = other.fd_;
        host_ = std::move(other.host_);
        port_ = other.port_;
        connected_ = other.connected_;

        other.fd_ = -1;
        other.connected_ = false;
    }
    return *this;
}

ssize_t Connection::send(const void* buf, size_t len)
{
    if (!connected_) {
        throw ConnectionError("Not connected");
    }

    ssize_t sent = ::send(fd_, buf, len, 0);
    if (sent < 0) {
        int saved_errno = errno;
        connected_ = false;
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
        connected_ = false;
        throw ConnectionError("Recv failed: " + std::string(strerror(saved_errno)));
    }

    if (received == 0) {
        // EOF - connection closed by peer
        connected_ = false;
    }

    return received;
}

/* Caller must hold send_lock_.  Same bytes-out-the-door loop as
 * send_exact() but without re-acquiring the mutex. */
void Connection::send_all_locked(const void* buf, size_t len)
{
    size_t total_sent = 0;
    const uint8_t* ptr = static_cast<const uint8_t*>(buf);

    while (total_sent < len) {
        ssize_t n = send(ptr + total_sent, len - total_sent);
        if (n == 0) {
            throw ConnectionError("Connection closed before sending all data");
        }
        total_sent += n;
    }
}

void Connection::send_exact(const void* buf, size_t len)
{
    /*
     * Serialise concurrent senders.  Multiple OSv block-layer threads
     * may submit Writes simultaneously; without this lock their
     * header and data bytes can interleave on the socket and the
     * downstairs disconnects with "bytes remaining on stream".
     */
    std::lock_guard<std::mutex> guard(send_lock_);
    send_all_locked(buf, len);
}

void Connection::send_exact_with_data(const void* header, size_t hlen,
                                      const void* data, size_t dlen)
{
    /*
     * Holding the lock across BOTH the header and the data sends is
     * what makes this atomic against concurrent senders -- a Crucible
     * Write frame is one logical unit on the wire even though it is
     * physically split between the bincode header (with the data
     * length prefix) and the data bytes themselves.
     */
    std::lock_guard<std::mutex> guard(send_lock_);
    send_all_locked(header, hlen);
    send_all_locked(data, dlen);
}

void Connection::recv_exact(void* buf, size_t len)
{
    std::lock_guard<std::mutex> guard(recv_lock_);

    size_t total_received = 0;
    uint8_t* ptr = static_cast<uint8_t*>(buf);

    while (total_received < len) {
        ssize_t n = recv(ptr + total_received, len - total_received);
        if (n == 0) {
            throw ConnectionError("Connection closed before receiving all data");
        }
        total_received += n;
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
