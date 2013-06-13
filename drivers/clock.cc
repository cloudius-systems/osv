#include <assert.h>
#include "clock.hh"

clock* clock::_c;

clock::~clock()
{
}

void clock::register_clock(clock* c)
{
    assert(!_c);
    _c = c;
}

clock* clock::get()
{
    return _c;
}
