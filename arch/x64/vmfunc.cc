#include <osv/vmfunc.hh>

long vmfunc(long p1, long p2, long p3, long n, long p4, long p5)
{
    long r = trampoline();
    return r;
}