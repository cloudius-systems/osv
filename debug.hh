#include <iostream>
#include "boost/format.hpp"

typedef boost::format fmt;

class IsaSerialConsole;

class Debug {
public:
    //friend std::ostream& operator<<(std::ostream&, const aa&);
    static Debug* Instance() {return (pinstance)? pinstance: (pinstance = new Debug);};
    void out(const char* msg, bool lf=true);
    void setConsole(IsaSerialConsole* console) {_console = console;};

private:
   Debug() :_console(0) {Debug::pinstance = 0;};
   Debug(const Debug& d) :_console(d._console) {};
   Debug& operator=(const Debug& d) {pinstance = d.pinstance; _console = d._console; return *pinstance;};

   IsaSerialConsole* _console;
   static Debug* pinstance;
};

void debug(const char *msg, bool lf=true);
void debug(const boost::format& fmt, bool lf=true);
void debug(std::string str, bool lf=true);

