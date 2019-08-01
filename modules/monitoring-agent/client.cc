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
#include <sstream>

namespace monitoring_agent {

using boost::asio::ip::tcp;
using boost::asio::deadline_timer;

class connection_exception : public std::exception {
public:
    connection_exception(const std::string& msg)
        : msg(msg)
    { }

    virtual const char* what() const throw () {
        return msg.c_str();
    }
private:
    std::string msg;
};

client::client(int _time_out)
    : status_code(0)
    , _socket(io_service)
    , time_out(_time_out)
{ }

template<typename Duration>
static void connect_with_timeout(boost::asio::io_service& io_service,
                                 boost::asio::ip::tcp::socket& _socket, tcp::endpoint& _endpoint,
                                 boost::system::error_code& ec, Duration duration)
{
    io_service.reset();

    deadline_timer timeout { io_service };
    timeout.expires_from_now(duration);
    timeout.async_wait([&] (const boost::system::error_code& ec) {
        if (ec != boost::asio::error::operation_aborted) {
            _socket.close();
        }
    });

    _socket.async_connect(_endpoint, [&](const boost::system::error_code& error_code) {
        timeout.cancel();
        ec = error_code;
    });

    io_service.run();
}

static void make_content(boost::asio::streambuf& content_buff,
                         const std::string& file_name, const std::string& dir,
                         const std::string& content, const std::string& boundry)
{
    std::ostream content_stream(&content_buff);
    content_stream << "--" << boundry << "\r\n";
    content_stream << "Content-Disposition: form-data; name=\"key\"\r\n\r\n";
    if (dir != "") {
        content_stream << dir << "/";
    }
    content_stream << file_name << "\r\n";
    content_stream << "--" << boundry <<"\r\n";;
    content_stream << "Content-Disposition: form-data; name=\"acl\"\r\n\r\n";
    content_stream << "public-read-write\r\n";
    content_stream << "--" << boundry <<"\r\n";;
    content_stream
            << "Content-Disposition: form-data; name=\"file\"; filename=\""
            << file_name << "\"\r\n";
    content_stream << "Content-Type: text/plain\r\n\r\n";
    content_stream << content << "\r\n\r\n";
    content_stream << "--" << boundry << "--\r\n";
}

client& client::upload(const std::string& server, const std::string& path,
                       unsigned int port, const std::string& file_name, const std::string& dir,
                       const std::string& content)
{
    boost::system::error_code ec;
    auto address = getaddr(server);

    tcp::endpoint _endpoint(address, port);

    connect_with_timeout(io_service, _socket, _endpoint, ec,
                         boost::posix_time::seconds(time_out));

    if (ec) {
        throw connection_exception(
            std::string("connect failed ") + ec.message());
    }

    boost::asio::streambuf request;
    std::ostream request_stream(&request);
    request_stream << "POST " << path << " HTTP/1.1\r\n";
    request_stream << "Host: " << server << "\r\n";
    request_stream << "Accept: */*\r\n";

    std::string boundry = "------------------------728cbce45e702b81";

    boost::asio::streambuf content_buff;
    make_content(content_buff, file_name, dir, content, boundry);

    request_stream << "Content-Length: " << std::to_string(content_buff.size())
                   << "\r\n";
    request_stream << "Expect: 100-continue\r\n";

    request_stream << "Content-Type: multipart/form-data; boundary=" << boundry
                   << "\r\n";
    request_stream << "\r\n";

    // Send the header.
    boost::asio::write(_socket, request);
    //send the body
    boost::asio::write(_socket, content_buff);

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
    while (std::getline(response_stream, header) && header != "\r")
        ;

    return *this;
}

boost::asio::ip::address client::getaddr(const std::string& server)
{
    boost::asio::ip::tcp::resolver resolver(io_service);
    boost::asio::ip::tcp::resolver::query query(server, "80");
    boost::system::error_code error;
    boost::asio::ip::tcp::resolver::iterator it(resolver.resolve(query, error));
    if (error)
        throw connection_exception(
            std::string("Unable to resolve address for ") + server);
    return it->endpoint().address();
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
