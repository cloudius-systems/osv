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

void debug(const char *msg, bool lf=true);
void debug(const boost::format& fmt, bool lf=true);
void debug(std::string str, bool lf=true);

