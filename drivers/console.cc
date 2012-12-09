#include "console.hh"

void Console::write(const boost::format& fmt)
{
    return write(fmt.str());
}

void Console::writeln(const boost::format& fmt)
{
    return writeln(fmt.str());
}

void Console::write(std::string str)
{
    return write(str.c_str());
}

void Console::writeln(std::string str)
{
    return writeln(str.c_str());
}
