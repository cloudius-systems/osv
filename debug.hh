#ifndef DEBUG_H
#define DEBUG_H

#include <iostream>
#include "boost/format.hpp"

typedef boost::format fmt;

class IsaSerialConsole;

class Debug {
public:
    //friend std::ostream& operator<<(std::ostream&, const aa&);
    static Debug* Instance() {return (pinstance)? pinstance: (pinstance = new Debug);};

private:
   Debug() {Debug::pinstance = 0;};
   Debug(const Debug& d) {};
   Debug& operator=(const Debug& d) {pinstance = d.pinstance; return *pinstance;};

   static Debug* pinstance;
};

extern "C" {void debug(const char *msg); }
void debug(const boost::format& fmt, bool lf=true);
void debug(std::string str, bool lf=true);

#endif // DEBUG_H
