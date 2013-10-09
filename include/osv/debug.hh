/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <iostream>
#include <map>
#include <string>
#include <cstdarg>
#include <osv/debug.h>
#include <osv/mutex.h>
#include <osv/printf.hh>
#include "boost/format.hpp"
#include "api/assert.h"

#define DEBUG_BUFFER_SIZE 1024*50 // 50kb buffer

#ifndef NDEBUG
/**
 * \note This assert will compile out completely when
 *       mode=release!!!
 *
 * DEBUG_ASSERT() will call for the assert() and will print the
 * given message if the condition evaluates to false.
 *
 * Use this macro for assert-style checking in a fast path code.
 * It will perform a checking when mode=debug and will be
 * compiled out when mode=release.
 *
 * DEBUG_ASSERT() accepts parameters in a printf-style manner
 * and prints the message to stderr.
 */
#define DEBUG_ASSERT(cond, msg, ...) \
do {                                                         \
    if (!(cond)) {                                           \
        fprintf(stderr, msg ": ", ##__VA_ARGS__);            \
        assert(0);                                           \
    }                                                        \
} while (0)
#else /* !NDEBUG */
#define DEBUG_ASSERT(cond, msg, ...) (void)0
#endif /* NDEBUG */

typedef boost::format fmt;

class IsaSerialConsole;

class logger {
public:

    logger();
    virtual ~logger();

    static logger* instance() {
        if (_instance == nullptr) {
            _instance = new logger();
        }
        return (_instance);
    }

    //
    // Interface for logging, these functions checks the filters and
    // calls the underlying debug functions.
    //
    void wrt(const char* tag, logger_severity severity, const boost::format& _fmt);
    void wrt(const char* tag, logger_severity severity, const char* _fmt, ...);
    void wrt(const char* tag, logger_severity severity, const char* _fmt, va_list ap);

private:
   static logger* _instance;

   bool parse_configuration(void);

   bool is_filtered(const char *tag, logger_severity severity);

   // Adds a tag, if it exists then modify severity
   void add_tag(const char* tag, logger_severity severity);

   // Returns severity, per tag, if doesn't exist return error as default
   logger_severity get_tag(const char* tag);
   const char* loggable_severity(logger_severity severity);

   std::map<std::string, logger_severity> _log_level;

   mutex _lock;
};

extern "C" {
    void debug(const char *msg);
    void debugf(const char *, ...);
}
void flush_debug_buffer();
void enable_verbose();
void debug(const boost::format& fmt);
template <typename... args>
void debug(boost::format& fmt, args... as);
void debug(std::string str);
template <typename... args>
void debug(const char* fmt, args... as);

extern "C" {void readln(char *msg, size_t size); }

template <typename... args>
void debug(const char* fmt, args... as)
{
    debug(osv::sprintf(fmt, as...));
}

template <typename... args>
void debug(boost::format& fmt, args... as)
{
    debug(osv::sprintf(fmt, as...));
}

void abort(const char *fmt, ...) __attribute__((noreturn));

#endif // DEBUG_H
