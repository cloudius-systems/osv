#include <cstring>
#include <iostream>
#include <iomanip>
#include "boost/format.hpp"
#include "drivers/console.hh"
#include "debug.hh"

using namespace std;

Debug* Debug::pinstance = 0;

extern "C" {

void debug(const char *msg)
{
    console::write(msg, strlen(msg), true);
}
}

void debug(std::string str, bool lf)
{
    console::write(str.c_str(), str.length(), lf);
}

void debug(const boost::format& fmt, bool lf)
{
    debug(fmt.str(), lf);
}


/*std::ostream& operator<<(std::ostream& os, const aa& a)
{
        os <<"print stream func " << a.geta() << " " << a.getname();

        return os;
}*/
