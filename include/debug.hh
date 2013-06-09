#ifndef DEBUG_H
#define DEBUG_H

#include <iostream>
#include <map>
#include <string>
#include <cstdarg>
#include <osv/debug.h>
#include <osv/mutex.h>
#include "boost/format.hpp"

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

extern "C" {void debug(const char *msg); }
void debug(const boost::format& fmt);
template <typename... args>
void debug(boost::format& fmt, args... as);
void debug(std::string str);
template <typename... args>
void debug(const char* fmt, args... as);

extern "C" {void readln(char *msg, size_t size); }

template <>
void debug(boost::format& fmt);

template <typename arg0, typename... args>
inline
void debug(boost::format& fmt, const arg0& a0, args... as)
{
    debug(fmt % a0, as...);
}

template <typename... args>
void debug(const char* fmt, args... as)
{
    boost::format f(fmt);
    debug(f, as...);
}

void abort(const char *msg) __attribute__((noreturn));

#endif // DEBUG_H
