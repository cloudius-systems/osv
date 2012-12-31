#include <math.h>

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
