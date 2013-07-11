#ifndef OSV_ILOG2_HH
#define OSV_ILOG2_HH

#include <cstdint>

constexpr unsigned ilog2_roundup_constexpr(std::uintmax_t n)
{
    return n <= 1 ? 0 : 1 + ilog2_roundup_constexpr((n >> 1) + (n & 1));
}

inline unsigned count_leading_zeros(unsigned n)
{
    return __builtin_clz(n);
}

inline unsigned count_leading_zeros(unsigned long n)
{
    return __builtin_clzl(n);
}

inline unsigned count_leading_zeros(unsigned long long n)
{
    return __builtin_clzll(n);
}

template <typename T>
inline
unsigned ilog2_roundup(T n)
{
    if (n <= 1) {
        return 0;
    }
    return sizeof(T)*8 - count_leading_zeros(n - 1);
}

template <typename T>
inline constexpr
bool is_power_of_two(T n)
{
    return (n & (n - 1)) == 0;
}

#endif
