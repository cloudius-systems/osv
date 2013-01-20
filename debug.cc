#include <cstring>
#include <iostream>
#include <iomanip>
#include "boost/format.hpp"
#include "drivers/console.hh"
#include "debug.hh"

using namespace std;

Debug* Debug::pinstance = 0;

void debug(const char *msg, bool lf)
{
    console::write(msg, strlen(msg), lf);
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
