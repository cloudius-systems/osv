//
// request_parser.hpp
// ~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef HTTP_REQUEST_PARSER_HPP
#define HTTP_REQUEST_PARSER_HPP

#include <tuple>

namespace http {

namespace server {

struct request;

/**
 * Parser for incoming requests.
 */
class request_parser {
public:
    /**
     * Construct ready to parse the request method.
     */
    request_parser();

    /**
     * Reset to initial parser state.
     */
    void reset();

    /**
     * Result of parse.
     */
    enum result_type {
        good, bad, indeterminate
    };

    /**
     * Parse some data. The enum return value is good when a complete request has
     * been parsed, bad if the data is invalid, indeterminate when more data is
     * required. The InputIterator return value indicates how much of the input
     * has been consumed.
     * @param req the request to be fill
     * @param begin iterator for the beginning for the parsing
     * @param end iterator for the end of the parsing
     * @return a tuple with the result type and the new position
     */
    template<typename Itr>
    std::tuple<result_type, Itr> parse(request& req, Itr begin, Itr end)
    {
        while (begin != end) {
            result_type result = consume(req, *begin++);
            if (result == good || result == bad)
                return std::make_tuple(result, begin);
        }
        return std::make_tuple(indeterminate, begin);
    }

private:
    /**
     * Handle the next character of input.
     * @param req teheq request to fill
     * @param input the next character
     * @return result type
     */
    result_type consume(request& req, char input);

    /**
     * Check if a byte is an HTTP character.
     * @param c the byte to check
     * @return true if it's an http charcter
     */
    static bool is_char(int c);

    /**
     * Check if a byte is an HTTP control character.
     *
     * @param c the byte to check
     * @return true if it's a control byte
     */
    static bool is_ctl(int c);

    /**
     * Check if a byte is defined as an HTTP tspecial character.
     *
     * @param c the byte to check
     * @return true if it is an http tspecial character
     */
    static bool is_tspecial(int c);

    /**
     * Check if a byte is a digit.
     *
     * @param c the byte to check
     * @return true if it's a digit
     */
    static bool is_digit(int c);

    /**
     * The current state of the parser.
     */
    enum state {
        method_start,             //!< method_start
        method,                   //!< method
        uri,                      //!< uri
        http_version_h,           //!< http_version_h
        http_version_t_1,         //!< http_version_t_1
        http_version_t_2,         //!< http_version_t_2
        http_version_p,           //!< http_version_p
        http_version_slash,       //!< http_version_slash
        http_version_major_start, //!< http_version_major_start
        http_version_major,       //!< http_version_major
        http_version_minor_start, //!< http_version_minor_start
        http_version_minor,       //!< http_version_minor
        expecting_newline_1,      //!< expecting_newline_1
        header_line_start,        //!< header_line_start
        header_lws,               //!< header_lws
        header_name,              //!< header_name
        space_before_header_value,              //!< space_before_header_value
        header_value,             //!< header_value
        expecting_newline_2,      //!< expecting_newline_2
        expecting_newline_3       //!< expecting_newline_3
    } state_;
};

} // namespace server

} // namespace http

#endif // HTTP_REQUEST_PARSER_HPP
