#ifndef CLOCK_HH_
#define CLOCK_HH_

#include <osv/types.h>

class clock {
public:
    virtual ~clock();
    virtual s64 time() = 0;
    static void register_clock(clock* c);
    static clock* get() __attribute__((no_instrument_function));
private:
    static clock* _c;
};

inline constexpr long long operator"" _ns(unsigned long long t)
{
    return t;
}

inline constexpr long long operator"" _us(unsigned long long t)
{
    return t * 1000_ns;
}

inline constexpr long long operator"" _ms(unsigned long long t)
{
    return t * 1000_us;
}

inline constexpr long long operator"" _s(unsigned long long t)
{
    return t * 1000_ms;
}

#endif /* CLOCK_HH_ */
