#include <osv/printf.hh>

namespace osv {

template <>
std::ostream& fprintf(std::ostream& os, boost::format& fmt)
{
    return os << fmt;
}

}
