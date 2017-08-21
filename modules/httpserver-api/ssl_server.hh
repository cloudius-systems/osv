/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _HTTP_SSL_SERVER_HH
#define _HTTP_SSL_SERVER_HH

#include "transport.hh"
#include <boost/asio/ssl.hpp>
#include <boost/asio.hpp>

namespace http {

namespace server {

using tcp = boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;

class ssl_acceptor : public acceptor
{
private:
    boost::asio::io_service& _io_service;
    ssl::context _ctx;
    tcp::acceptor _tcp_acceptor;
public:
    ssl_acceptor(boost::asio::io_service& io_service,
            boost::asio::ssl::context&& ctx,
            tcp::acceptor&& tcp_acceptor);

    void do_accept(callback_t on_connected) override;
    void close() override;
};

ssl::context make_ssl_context(const std::string& ca_cert_path,
    const std::string& cert_path, const std::string& key_path);

}

}

#endif
