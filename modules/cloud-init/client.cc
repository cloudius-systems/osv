/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "client.hh"
#include <boost/asio/ip/address.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <fstream>

namespace init {

using boost::asio::ip::tcp;
using boost::asio::deadline_timer;

class connection_exception : public std::exception {
public:
    connection_exception(const std::string& msg)
        : msg(msg)
    {
    }
    virtual const char* what() const throw ()
    {
        return msg.c_str();
    }
private:
    std::string msg;
};

client::client()
    : status_code(0), _socket(io_service)
{

}

void client::set_header(const std::string& name, const std::string& value)
{
    _headers[name] = value;
}

template<typename Duration>
static void connect_with_timeout(boost::asio::io_service& io_service,
    boost::asio::ip::tcp::socket& _socket, tcp::endpoint& _endpoint,
    boost::system::error_code& ec, Duration duration)
{
    io_service.reset();

    deadline_timer timeout{io_service};
    timeout.expires_from_now(duration);
    timeout.async_wait([&] (const boost::system::error_code& ec) {
        if (ec != boost::asio::error::operation_aborted) {
            _socket.close();
        }
    });

    _socket.async_connect(_endpoint, [&] (const boost::system::error_code& error_code) {
        timeout.cancel();
        ec = error_code;
    });

    io_service.run();
}

client& client::get(const std::string& server, const std::string& path,
                    unsigned int port)
{
    boost::system::error_code ec;
    boost::asio::ip::address_v4 v4_address =
        boost::asio::ip::address_v4::from_string(server, ec);
    if (ec) {
        throw connection_exception(
            std::string("Bad address ") + server + ec.message());
    }
    boost::asio::ip::address address(v4_address);

    tcp::endpoint _endpoint(address, port);

    connect_with_timeout(io_service, _socket, _endpoint, ec, boost::posix_time::seconds(1));

    if (ec != 0) {
        throw connection_exception(
            std::string("connect failed ") + ec.message());
    }

    // Form the request. We specify the "Connection: close" header so that the
    // server will close the socket after transmitting the response. This will
    // allow us to treat all data up until the EOF as the content.
    boost::asio::streambuf request;
    std::ostream request_stream(&request);
    request_stream << "GET " << path << " HTTP/1.0\r\n";

    std::unordered_map<std::string,std::string> default_headers;
    default_headers["Host"] = server;
    default_headers["Accept"] = "*/*";
    default_headers["Connection"] = "close";

    for (auto& e : _headers) {
        request_stream << e.first << ": " << e.second << "\r\n";
    }

    for (auto& e : default_headers) {
        if (!_headers.count(e.first)) {
            request_stream << e.first << ": " << e.second << "\r\n";
        }
    }

    request_stream << "\r\n";

    // Send the request.
    boost::asio::write(_socket, request);

    // Read the response status line. The response streambuf will automatically
    // grow to accommodate the entire line. The growth may be limited by passing
    // a maximum size to the streambuf constructor.
    boost::asio::read_until(_socket, response, "\r\n");
    std::istream response_stream(&response);
    std::string http_version;
    response_stream >> http_version;

    response_stream >> status_code;
    std::string status_message;
    std::getline(response_stream, status_message);
    if (!response_stream || http_version.substr(0, 5) != "HTTP/")
    {
        done = true;
        throw connection_exception(
            std::string("Bad HTTP version ") + http_version);
    }
    // Read the response headers, which are terminated by a blank line.
    boost::asio::read_until(_socket, response, "\r\n\r\n");

    // Process the response headers.
    std::string header;
    while (std::getline(response_stream, header) && header != "\r");

    return *this;
}

std::ostream& operator<<(std::ostream& os, client& c)
{
    if (c.done) {
        return os;
    }
    if (c.response.size() > 0) {
        os << &c.response;
    }
    boost::system::error_code error;
    while (boost::asio::read(c._socket, c.response,
                             boost::asio::transfer_at_least(1), error)) {
        os << &c.response;
    }
    c.done = true;
    if (error != boost::asio::error::eof) {
        throw boost::system::system_error(error);
    }
    return os;
}

}
