#include <math.h>
#include "types.hh"

// FIXME: check for sse4.1

double ceil(double v)
{
    double r;
    asm("roundsd $2, %1, %0" : "=x"(r) : "x"(v));
    return r;
}

double floor(double v)
{
    double r;
    asm("roundsd $1, %1, %0" : "=x"(r) : "x"(v));
    return r;
}

int __isnan(double v)
{
    u64 r;
    asm("cmpunordsd %1, %1; movq %1, %0" : "=rm"(r), "+x"(v));
    return r & 1;
}
