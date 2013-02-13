#ifndef DEBUG_H
#define DEBUG_H

#include <iostream>
#include <map>
#include <string>
#include "boost/format.hpp"

typedef boost::format fmt;

class IsaSerialConsole;

class logger {
public:

   enum logger_severity {
        logger_debug = 0,
        logger_info = 1,
        logger_warn = 2,
        logger_error = 3,
        // Suppress output, even errors
        logger_none = 4
    };

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
    void log(const char* tag, logger_severity severity, const boost::format& _fmt);
    void log(const char* tag, logger_severity severity, const char* _fmt, ...);

private:
   static logger* _instance;

   bool parse_configuration(void);

   bool is_filtered(const char *tag, logger_severity severity);

   // Adds a tag, if it exists then modify severity
   void add_tag(const char* tag, logger_severity severity);

   // Returns severity, per tag, if doesn't exist return error as default
   logger_severity get_tag(const char* tag);

   std::map<std::string, logger_severity> _log_level;

};

extern "C" {void debug(const char *msg); }
void debug(const boost::format& fmt, bool lf=true);
void debug(std::string str, bool lf=true);

#endif // DEBUG_H
