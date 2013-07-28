#ifndef PRINTF_HH_
#define PRINTF_HH_

#include <boost/format.hpp>
#include <string>
#include <sstream>
#include <iostream>

namespace osv {

template <typename... args>
std::ostream& fprintf(std::ostream& os, boost::format& fmt, args... as);

template <typename... args>
std::string sprintf(const char* fmt, args... as);

template <typename... args>
std::string sprintf(boost::format& fmt, args... as);

// implementation

template <>
std::ostream& fprintf(std::ostream& os, boost::format& fmt);

template <typename arg0, typename... args>
inline
std::ostream& fprintf(std::ostream& os, boost::format& fmt, const arg0& a0, args... as)
{
    return fprintf(os, fmt % a0, as...);
}

template <typename... args>
std::ostream& fprintf(std::ostream& os, const char* fmt, args... as)
{
    boost::format f(fmt);
    return fprintf(os, f, as...);
}

template <typename... args>
std::string sprintf(const char* fmt, args... as)
{
    boost::format f(fmt);
    std::ostringstream os;
    fprintf(os, f, as...);
    return os.str();
}

template <typename... args>
std::string sprintf(boost::format& fmt, args... as)
{
    std::ostringstream os;
    fprintf(os, fmt, as...);
    return os.str();
}

} // namespace osv

#endif /* PRINTF_HH_ */
