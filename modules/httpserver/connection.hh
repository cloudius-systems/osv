//
// connection.hpp
// ~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef HTTP_CONNECTION_HPP
#define HTTP_CONNECTION_HPP

#include "request_handler.hh"
#include "request_parser.hh"
#include "request.hh"
#include "reply.hh"
#include "transport.hh"

#include <boost/asio.hpp>
#include <array>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>

namespace http {

namespace server {

typedef std::array<char, 8192> buffer_type;
typedef std::function<void(connection& connect)> connection_function;

class connection_manager;

class multipart_parser {
public:
    multipart_parser(connection* connect);
    void set_boundary(const std::string& _boundary);

    request_parser::result_type parse(request& req, buffer_type::pointer & bg,
                                      const buffer_type::pointer & end);

    request_parser::result_type parse_in_message(request& req)
    {
        buffer_type::pointer bg = in_message_content.data();
        return parse(req, bg, end);
    }

    void close()
    {
        upload_file.close();
        set_mode(CLOSED);
    }

    bool is_done()
    {
        return mode == DONE || mode == CLOSED;
    }

    /**
     * Set the name of the temporary file that will be used
     * by the multipart parser to upload a file.
     * @param name the name of the temporary file
     */
    void set_tmp_file(const std::string& name)
    {
        use_file = true;
        this->name = name;
    }

    void set_in_message(const buffer_type::pointer& bg,
                        const buffer_type::pointer& end);

    enum reading_mode {
        WAIT_BOUNDARY,
        WAIT_CONTENT_DISPOSITION,
        WAIT_EMPTY,
        WRITE_TO_FILE,
        DONE,
        CLOSED
    };

    void set_call_back(reading_mode mode, connection_function fun)
    {
        func[mode] = fun;
    }

    std::stringstream stream;
private:
    connection_function func[CLOSED + 1] = { };
    void set_original_file(request& req, const std::string val);

    void open_tmp_file(request& req);

    void set_mode(reading_mode m)
    {
        mode = m;
        if (func[mode] != nullptr) {
            func[mode](connect);
        }
    }

    reading_mode mode;

    size_t empty_lines;

    size_t pos_in_file;

    std::string boundary;

    std::ofstream upload_file;

    size_t header_length;

    std::string name;

    buffer_type in_message_content;

    buffer_type::pointer end;

    connection& connect;

    bool use_file;
};

/**
 * Represents a single connection from a client.
 *
 */
class connection : public std::enable_shared_from_this<connection> {
public:
    /**
     * Construct a connection with the given socket.
     *
     * @param socket the new socket
     * @param manager the connection manager
     * @param handler the request handler
     */
    explicit connection(std::shared_ptr<transport> transport,
                        connection_manager& manager, request_handler& handler);

    connection(const connection&) = delete;
    connection& operator=(const connection&) = delete;

    /**
     * Start the first asynchronous operation for the connection.
     *
     */
    void start();

    /**
     * Stop all asynchronous operations associated with the connection.
     *
     */
    void stop();

    /**
     * Start parse and upload a multipart message
     */
    void upload();

    multipart_parser& get_multipart_parser()
    {
        return multipart;
    }

    request& get_request()
    {
        return request_;
    }

    reply& get_reply()
    {
        return reply_;
    }
private:
    /**
     * Perform an asynchronous read operation.
     *
     */
    void do_read();

    /**
     * Perform an asynchronous read operation of a multipart.
     * and save it to file
     *
     */
    void do_read_mp();

    /**
     * Perform an asynchronous write operation.
     *
     */
    void do_write();

    /**
     * check if the request has content
     * and update it accordingly.
     * @return false on error
     */
    bool set_content_type();

    /**
     * Transport for the connection.
     *
     */
    std::shared_ptr<transport> transport_;

    /**
     * The manager for this connection.
     *
     */
    connection_manager& connection_manager_;

    /**
     * The handler used to process the incoming request.
     *
     */
    request_handler& request_handler_;

    /**
     * Buffer for incoming data.
     *
     */
    buffer_type buffer_;

    /**
     * The incoming request.
     *
     */
    request request_;

    /**
     * The parser for the incoming request.
     *
     */
    request_parser request_parser_;

    /**
     * The reply to be sent back to the client.
     *
     */
    reply reply_;

    multipart_parser multipart;

    bool delayed_reply;

};

typedef std::shared_ptr<connection> connection_ptr;

} // namespace server

} // namespace http

#endif // HTTP_CONNECTION_HPP
