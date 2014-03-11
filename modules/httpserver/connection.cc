//
// connection.cpp
// ~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "connection.hh"

#include "connection_manager.hh"
#include "request_handler.hh"

#include <utility>
#include <vector>
#include <exception>
#include <sstream>

namespace http {

namespace server {

class message_handling_exception : public std::exception {
public:
    message_handling_exception(const std::string& msg)
        : _msg(msg)
    {
    }
    virtual const char* what() const throw ()
    {
        return _msg.c_str();
    }

private:
    std::string _msg;
};

/**
 * This helper function reads a string from a buffer.
 * It reads till an end of line and consume the FF/NL chars
 * @param b buffer pointer
 * @return a string
 */
static std::string buf_str(buffer_type::pointer & b,
                           const buffer_type::pointer & end)
{
    char c;
    std::stringstream res;
    while (b < end && (c = *b) != '\r' && c != '\n') {
        res << c;
        b++;
    }
    if (b >= end) {
        throw message_handling_exception("Error parsing message, missing EOL");
    }

    if (c == '\r' || c == '\n') {
        b++; // consume the new line
    }
    return res.str();
}

connection::connection(boost::asio::ip::tcp::socket socket,
                       connection_manager& manager, request_handler& handler)
    : socket_(std::move(socket))
    , connection_manager_(manager)
    , request_handler_(handler)
{
}

void connection::start()
{
    do_read();
}

void connection::stop()
{
    socket_.close();
}

void connection::do_read()
{
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(buffer_),
                            [this, self](boost::system::error_code ec,
                                         std::size_t bytes_transferred)
    {
        if (!ec)
        {
            request_parser::result_type result;
            std::tie(result, std::ignore) = request_parser_.parse(
                                                request_, buffer_.data(), buffer_.data() +
                                                bytes_transferred);

            if (result == request_parser::good)
            {
                request_handler_.handle_request(request_, reply_);
                do_write();
            }
            else if (result == request_parser::bad)
            {
                reply_ = reply::stock_reply(reply::bad_request);
                do_write();
            }
            else
            {
                do_read();
            }
        }
        else if (ec != boost::asio::error::operation_aborted)
        {
            connection_manager_.stop(shared_from_this());
        }
    });
}

void connection::do_write()
{
    auto self(shared_from_this());
    boost::asio::async_write(socket_, reply_.to_buffers(),
                             [this, self](boost::system::error_code ec, std::size_t)
    {
        if (!ec)
        {
            // Initiate graceful connection closure.
            boost::system::error_code ignored_ec;
            socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both,
                             ignored_ec);
        }

        if (ec != boost::asio::error::operation_aborted)
        {
            connection_manager_.stop(shared_from_this());
        }
    });
}

} // namespace server

} // namespace http
