/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "transport.hh"
#include "plain_server.hh"
#include <boost/asio.hpp>

namespace http {

namespace server {

using tcp = boost::asio::ip::tcp;
using socket_t = plain_acceptor::socket_t;

class plain_transport : public transport
{
private:
    socket_t _socket;
public:
    plain_transport(socket_t&& socket)
        : _socket(std::move(socket))
    {
    }

    void close(std::function<void(boost::system::error_code)> callback) override
    {
        _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send);
        _socket.close();
        callback(boost::system::error_code());
    }

    void async_read_some(boost::asio::mutable_buffers_1 buf,
        std::function<void(boost::system::error_code, std::size_t)>&& callback) override
    {
        _socket.async_read_some(buf, std::move(callback));
    }

    void async_write(std::vector<boost::asio::const_buffer> buffers,
        std::function<void(boost::system::error_code, std::size_t)>&& callback) override
    {
        boost::asio::async_write(_socket, buffers, std::move(callback));
    }

    std::string get_protocol_name()
    {
        return "http";
    }
};

plain_acceptor::plain_acceptor(boost::asio::io_service& io_service,
            tcp::acceptor&& tcp_acceptor)
    : _io_service(io_service)
    , _tcp_acceptor(std::move(tcp_acceptor))
    , _socket(_io_service)
{
}

void plain_acceptor::do_accept(callback_t on_connected)
{
    _tcp_acceptor.async_accept(_socket, [this, on_connected] (boost::system::error_code ec) {
        if (!_tcp_acceptor.is_open()) {
            return;
        }

        if (!ec) {
            on_connected(std::make_shared<plain_transport>(std::move(_socket)));
        }

        do_accept(on_connected);
    });
}

void plain_acceptor::close()
{
    _tcp_acceptor.close();
}

}

}
