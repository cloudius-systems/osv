//
// reply.hpp
// ~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef HTTP_REPLY_HPP
#define HTTP_REPLY_HPP

#include "header.hh"

#include <string>
#include <vector>
#include <boost/asio.hpp>

namespace http {

namespace server {

/**
 * A reply to be sent to a client.
 */
struct reply {
    /**
     * The status of the reply.
     */
    enum status_type {
        ok                    = 200, //!< ok
        created               = 201, //!< created
        accepted              = 202, //!< accepted
        no_content            = 204, //!< no_content
        multiple_choices      = 300, //!< multiple_choices
        moved_permanently     = 301, //!< moved_permanently
        moved_temporarily     = 302, //!< moved_temporarily
        not_modified          = 304, //!< not_modified
        bad_request           = 400, //!< bad_request
        unauthorized          = 401, //!< unauthorized
        forbidden             = 403, //!< forbidden
        not_found             = 404, //!< not_found
        internal_server_error = 500, //!< internal_server_error
        not_implemented       = 501, //!< not_implemented
        bad_gateway           = 502, //!< bad_gateway
        service_unavailable   = 503  //!< service_unavailable
    } status;

    /**
     * The headers to be included in the reply.
     */
    std::vector<header> headers;

    /**
     * The content to be sent in the reply.
     */
    std::string content;

    /**
     * Convert the reply into a vector of buffers.
     * The buffers do not own the
     * underlying memory blocks, therefore the reply object must remain valid and
     * not be changed until the write operation has completed.
     *
     * @return a vector of buffer to be sent as a reply
     */
    std::vector<boost::asio::const_buffer> to_buffers();

    /**
     * Get a stock reply.
     * set the reply headers according to the content
     * and the status
     * @param status http status code
     * @param content the reply content
     * @return the reply to be sent to the client
     */
    static reply stock_reply(status_type status, const std::string* content = nullptr);
};

} // namespace server

} // namespace http

#endif // HTTP_REPLY_HPP
