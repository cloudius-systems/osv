#ifndef ARCH_BITOPS_H_
#define ARCH_BITOPS_H_
static inline unsigned int 
bsrl(unsigned int mask)
{
    unsigned int result;
    asm volatile("bsrl %1,%0" : "=r" (result) : "rm" (mask));
    return result;
}

static inline int
fls(int mask)
{
    return (mask == 0 ? mask : (int)bsrl((unsigned int)mask) + 1);
}
#endif
