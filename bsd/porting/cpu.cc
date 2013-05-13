
#include "sched.hh"

extern "C" int get_cpuid(void)
{
    return sched::cpu::current()->id;
}
