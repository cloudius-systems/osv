#include <math.h>
#include <osv/types.h>

extern "C"
int __isnan(double v)
{
    u64 r;
    asm("cmpunordsd %1, %1; movq %1, %0" : "=rm"(r), "+x"(v));
    return r & 1;
}
