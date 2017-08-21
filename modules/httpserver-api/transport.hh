/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _HTTP_TRANSPORT_HH
#define _HTTP_TRANSPORT_HH

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <functional>
#include <memory>

namespace http {

namespace server {

class transport {
public:
    virtual void close(std::function<void(boost::system::error_code)> callback) = 0;

    virtual void async_read_some(boost::asio::mutable_buffers_1 buf,
        std::function<void(boost::system::error_code, std::size_t)>&& callback) = 0;

    virtual void async_write(std::vector<boost::asio::const_buffer> buffers,
        std::function<void(boost::system::error_code, std::size_t)>&& callback) = 0;

    virtual std::string get_protocol_name() = 0;
};

class acceptor {
public:
    using callback_t = std::function<void(std::shared_ptr<transport>)>;

    virtual void do_accept(callback_t on_connected) = 0;
    virtual void close() = 0;
};

}

}

#endif
