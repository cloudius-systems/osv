#ifndef OSV_ILOG2_HH
#define OSV_ILOG2_HH

#include <cstdint>

constexpr unsigned ilog2_roundup_constexpr(std::uintmax_t n)
{
    return n <= 1 ? 0 : 1 + ilog2_roundup_constexpr((n >> 1) + (n & 1));
}

#endif
