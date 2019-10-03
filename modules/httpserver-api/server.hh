//
// server.hpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
//   This file was modified from its original

#ifndef HTTP_SERVER_HH
#define HTTP_SERVER_HH

#include "connection_manager.hh"
#include "request_handler.hh"
#include "connection.hh"
#include "transport.hh"

#include <boost/asio.hpp>
#include <string>
#include <memory>

/**
 * The original server implementation was modified to get its configuration
 * from the server_config and uses the routes object for url routing
 */
namespace http {

namespace server {

/**
 * The top-level class of the HTTP server.
 */
class server {
public:

    /**
     * Construct the server to listen on the specified TCP address and port, and
     * serve up files from the given directory.
     *
     * @param config a configuration object
     * @param routes the routes object
     */
    explicit server(std::map<std::string,std::vector<std::string>> &config,
                    httpserver::routes* routes);

    server(const server&) = delete;

    server& operator=(const server&) = delete;

    /**
     * Run the server's io_service loop.
     *
     */
    void run();

    void close();
private:
    void on_connected(std::shared_ptr<transport> t);

    /**
     * The io_service used to perform asynchronous operations.
     *
     */
    boost::asio::io_service io_service_;

    /**
     * Acceptor used to listen for incoming connections.
     *
     */
    std::unique_ptr<acceptor> acceptor_;

    /**
     * The connection manager which owns all live connections.
     *
     */
    connection_manager connection_manager_;

    /**
     * The handler for all incoming requests.
     *
     */
    request_handler request_handler_;
};

} // namespace server

} // namespace http

#endif // HTTP_SERVER_HH
