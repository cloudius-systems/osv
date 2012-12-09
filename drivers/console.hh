#ifndef DRIVERS_CONSOLE_HH
#define DRIVERS_CONSOLE_HH

#include <boost/format.hpp>

class Console {
public:
    virtual ~Console() {}
    virtual void write(const char *str) = 0;
    virtual void newline() = 0;
    void writeln(const char *str) { write(str); newline(); }
    void write(std::string str);
    void writeln(std::string str);
    void write(const boost::format& fmt);
    void writeln(const boost::format& fmt);
};

#endif
