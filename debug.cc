#include <cstring>
#include <iostream>
#include <iomanip>
#include "drivers/isa-serial.hh"
#include "boost/format.hpp"
#include "debug.hh"

using namespace std;

Debug* Debug::pinstance = 0;

void Debug::out(const char* msg, bool lf)
{
    if (!_console)
        return;
    _console->write(msg, strlen(msg));
    if (lf)
        _console->newline();
}

void debug(const char *msg, bool lf)
{
    Debug::Instance()->out(msg, lf);
}

void debug(std::string str, bool lf)
{
    debug(str.c_str(), lf);
}

void debug(const boost::format& fmt, bool lf) {
    debug(fmt.str(), lf);
}


/*std::ostream& operator<<(std::ostream& os, const aa& a)
{
        os <<"print stream func " << a.geta() << " " << a.getname();

        return os;
}*/
