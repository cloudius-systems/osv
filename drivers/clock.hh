#ifndef CLOCK_HH_
#define CLOCK_HH_

#include <osv/types.h>

struct pvclock_wall_clock {
        u32   version;
        u32   sec;
        u32   nsec;
} __attribute__((__packed__));

struct pvclock_vcpu_time_info {
         u32   version;
         u32   pad0;
         u64   tsc_timestamp;
         u64   system_time;
         u32   tsc_to_system_mul;
         s8    tsc_shift;
         u8    flags;
         u8    pad[2];
} __attribute__((__packed__)); /* 32 bytes */

class clock {
public:
    virtual ~clock();
    virtual u64 time() = 0;
    static void register_clock(clock* c);
    static clock* get() __attribute__((no_instrument_function));
private:
    static clock* _c;
};

inline constexpr unsigned long long operator"" _ns(unsigned long long t)
{
    return t;
}

inline constexpr unsigned long long operator"" _us(unsigned long long t)
{
    return t * 1000_ns;
}

inline constexpr unsigned long long operator"" _ms(unsigned long long t)
{
    return t * 1000_us;
}

inline constexpr unsigned long long operator"" _s(unsigned long long t)
{
    return t * 1000_ms;
}

#endif /* CLOCK_HH_ */
