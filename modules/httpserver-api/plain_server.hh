/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _HTTP_PLAIN_SERVER_HH
#define _HTTP_PLAIN_SERVER_HH

#include "transport.hh"
#include <boost/asio.hpp>

namespace http {

namespace server {

using tcp = boost::asio::ip::tcp;

class plain_acceptor : public acceptor
{
public:
    using socket_t = tcp::socket;
private:
    boost::asio::io_service& _io_service;
    tcp::acceptor _tcp_acceptor;
    socket_t _socket;
public:
    plain_acceptor(boost::asio::io_service& io_service, tcp::acceptor&& tcp_acceptor);
    void do_accept(callback_t on_connected) override;
    void close() override;
};

}

}

#endif
