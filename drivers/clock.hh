#ifndef CLOCK_HH_
#define CLOCK_HH_

#include <osv/types.h>

class clock {
public:
    virtual ~clock();
    virtual u64 time() = 0;
    static void register_clock(clock* c);
    static clock* get();
private:
    static clock* _c;
};


#endif /* CLOCK_HH_ */
