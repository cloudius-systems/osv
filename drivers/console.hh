#ifndef DRIVERS_CONSOLE_HH
#define DRIVERS_CONSOLE_HH

#include <boost/format.hpp>

class Console {
public:
    virtual ~Console() {}
    virtual void write(const char *str, size_t len) = 0;
    virtual bool input_ready() = 0;
    virtual char readch() = 0;
    virtual void newline() = 0;
};

namespace console {

void write(const char *msg, size_t len, bool lf);
void write_ll(const char *msg, size_t len);
void console_init(void);

}

#endif
