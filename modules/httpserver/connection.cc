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

/**
 * Generate a temporary file name
 * @return a temporary file name
 */
static std::string tmp_file()
{
    char buffer [L_tmpnam];

    tmpnam (buffer);

    return buffer;
}

multipart_parser::multipart_parser()
    :
    mode(WAIT_BOUNDARY),
    empty_lines(0),
    pos_in_file(0),
    header_length(0)

{

}

void multipart_parser::set_boundary(const std::string& _boundary)
{
    boundary = _boundary;
}

void multipart_parser::set_original_file(request& req, const std::string val)
{
    auto p = val.find("filename=\"") + 10;
    auto end_name = val.find("\"", p + 1);
    std::string orig_fname = val.substr(p, end_name - p);

    req.headers.push_back(header());
    req.headers.back().name = "original_file_name";
    req.headers.back().value = orig_fname;
}

void multipart_parser::open_tmp_file(request& req)
{
    std::string fname = tmp_file();
    req.headers.push_back(header());
    req.headers.back().name = "file_name";
    req.headers.back().value = fname;
    upload_file.open(fname, std::ios::binary | std::ios::out);
    if (!upload_file.is_open() || upload_file.bad()) {
        std::cerr << "failed opening file for output " << fname << std::endl;
        throw message_handling_exception(
            "Failed opening temporary file for output");
    }
}

request_parser::result_type multipart_parser::parse(request& req,
        buffer_type::pointer & bg,
        const buffer_type::pointer & end)
{
    if (mode == DONE) {
        return request_parser::good;
    }
    std::string cur;
    buffer_type::pointer start_pos = bg;
    while (bg < end) {
        switch (mode) {
        case WAIT_BOUNDARY:
            if (buf_str(bg, end).find(boundary) != std::string::npos) {
                mode = WAIT_CONTENT_DISPOSITION;
            }
            break;

        case WAIT_CONTENT_DISPOSITION:
            if ((cur = buf_str(bg, end)).find("Content-Disposition")
                    != std::string::npos) {
                set_original_file(req, cur);
                open_tmp_file(req);
                mode = WAIT_EMPTY;
                empty_lines = 0;
            }
            break;

        case WAIT_EMPTY:
            if ((cur = buf_str(bg, end)) == "") {
                empty_lines++;
            } else {
                empty_lines = 0;
            }

            if (empty_lines >= 3) {
                mode = WRITE_TO_FILE;
                header_length += (bg - start_pos);
                req.content_length -= header_length;
            }
            break;

        case WRITE_TO_FILE:
            if (pos_in_file >= req.content_length) {
                mode = DONE;
                return request_parser::good;
            }
            {
                char c = *bg;
                upload_file << c;
                bg++;
                pos_in_file++;
            }
            break;

        default:
            break;
        }
    }
    header_length += (bg - start_pos);
    return request_parser::indeterminate;
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
